#include <stdio.h>      // fprintf, fopen, fclose, fflush, perror
#include <unistd.h>     // sleep, sysconf
#include <stdlib.h>     // calloc, free
#include <errno.h>      // errno, EACCES
#include <string.h>     // memset
#include <time.h>       // time, localtime_r, strftime, clock_gettime
#include <signal.h>     // sigaction, SIGINT, SIGTERM, sig_atomic_t
#include "args.h"       // struct config, parse_args
#include "output.h"     // print_header, print_row
#include "proc.h"       // proc_read, read_system_uptime, struct proc_sample

/* State kept for each monitored PID across cycles */
struct monitored {
    pid_t pid;
    int alive;                        /* 1 = still monitoring, 0 = gone/inaccessible */
    int have_prev;                    /* 0 until first sample -> CPU% shown as N/A */
    unsigned long long prev_ticks;    /* utime + stime from the previous cycle */
    double prev_time;                 /* monotonic time of that sample, in seconds */
    unsigned long long starttime;     /* identifies the process; changes if the PID is reused */
};

/* Set by the SIGINT/SIGTERM handler, checked by the main loop.
 * volatile: the compiler must re-read it every time (it changes "behind its back").
 * sig_atomic_t: guaranteed to be written in one step, so it can't be half-updated. */
static volatile sig_atomic_t stop = 0;

static void on_stop_signal(int sig)
{
    (void)sig;   /* unused parameter, silences -Wextra */
    stop = 1;    /* only set a flag: printf/free/fclose are not safe inside a handler */
}

int main(int argc, char *argv[])
{
    struct config cfg = { 0 };
    struct monitored *mons = NULL;
    FILE *outf = stdout;
    int rc = 1; // return code

    int a = parse_args(argc, argv, &cfg);
    if (a == ARGS_EXIT) {
        rc = 0;                         /* e.g. -h: not an error */
        goto out;
    }
    if (a == ARGS_ERROR)
        goto out;                       /* message already printed, rc stays 1 */

    /* Open the output file if -o was given */
    if (cfg.outfile) {
        outf = fopen(cfg.outfile, "a");
        if (!outf) {
            perror(cfg.outfile);
            outf = stdout;
            goto out;
        }
    }

    long clk = sysconf(_SC_CLK_TCK);
    if (clk <= 0) {
        fprintf(stderr, "sysconf(_SC_CLK_TCK) failed\n");
        goto out;
    }

    mons = calloc(cfg.npids, sizeof *mons);
    if (!mons) {
        perror("calloc");
        goto out;
    }

    for (size_t i = 0; i < cfg.npids; i++) {
        mons[i].pid = cfg.pids[i];
        mons[i].alive = 1;
    }

    print_header(outf, cfg.csv);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        goto out;
    }

    for (long cycle = 0; !stop && (cfg.count < 0 || cycle < cfg.count); cycle++) {
        /* a) timestamp string */
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char tstr[32];
        strftime(tstr, sizeof tstr, "%Y-%m-%d %H:%M:%S", &tm);

        /* b) system uptime */
        double sys_up;
        if (read_system_uptime(&sys_up) != 0) {
            fprintf(stderr, "Failed to read system uptime\n");
            goto out;
        }

        /* c) monotonic now for CPU% */
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            perror("clock_gettime");
            goto out;
        }
        double now_mono = ts.tv_sec + ts.tv_nsec / 1e9;

        /* d) sample each alive PID */
        size_t alive_count = 0;
        for (size_t i = 0; i < cfg.npids; i++) {
            if (!mons[i].alive)
                continue;

            struct proc_sample s;
            if (proc_read(mons[i].pid, &s) != 0) {
                if (!mons[i].have_prev)
                    fprintf(stderr, "PID %d: %s\n", (int)mons[i].pid,
                            errno == EACCES ? "permission denied" : "no such process");
                else
                    fprintf(stderr, "PID %d: process terminated, no longer monitored\n",
                            (int)mons[i].pid);
                mons[i].alive = 0;
                continue;
            }

            if (mons[i].have_prev && s.starttime != mons[i].starttime) {
                // handles the following scenario:
                // same PID, different start time: the original process exited and the kernel reused its PID for a new process
                fprintf(stderr, "PID %d: process terminated (PID reused by another process), no longer monitored\n", (int)mons[i].pid);
                mons[i].alive = 0;
                continue;
            }
            mons[i].starttime = s.starttime;

            unsigned long long ticks = s.utime + s.stime;

            /* CPU % */
            int cpu_valid = 0;
            double cpu = 0.0;
            if (mons[i].have_prev && ticks >= mons[i].prev_ticks && now_mono > mons[i].prev_time) {
                cpu = (ticks - mons[i].prev_ticks) / (double)clk / (now_mono - mons[i].prev_time) * 100.0;
                cpu_valid = 1;
            }

            mons[i].prev_ticks = ticks;
            mons[i].prev_time = now_mono;
            mons[i].have_prev = 1;

            /* uptime in seconds */
            long up = (long)(sys_up - s.starttime / (double)clk);
            if (up < 0)
                up = 0;

            /* memory in MB */
            double mem_mb = s.rss_kb / 1024.0;

            print_row(outf, cfg.csv, tstr, mons[i].pid, s.name, cpu_valid, cpu, mem_mb, up);

            alive_count++;
        }

        /* e) all exited? */
        if (alive_count == 0) {
            if (cycle == 0) {
                fprintf(stderr, "None of the given PIDs could be monitored\n");
                goto out;                       /* rc stays 1 */
            }
            fprintf(stderr, "All monitored processes have exited\n");
            break;                              /* normal end, rc = 0 */
        }

        /* write the cycle out now and catch write errors (disk full etc.) */
        if (fflush(outf) == EOF || ferror(outf)) {
            perror("write");
            goto out;
        }

        /* f) sleep unless last cycle */
        if (cfg.count < 0 || cycle + 1 < cfg.count)
            sleep(cfg.interval);
    }

    if (stop)
        fprintf(stderr, "\nStop signal received, exiting.\n");

    rc = 0;

out:
    if (outf != stdout)
        fclose(outf);
    free(mons);
    free(cfg.pids);
    return rc;
}