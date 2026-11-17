#include <stdio.h>      // printf, fprintf, fopen, fclose, fgets, perror
#include <unistd.h>     // getopt, optarg, optind, sleep, sysconf
#include <stdlib.h>     // strtol, malloc, calloc, free
#include <errno.h>      // errno, ERANGE, EACCES
#include <limits.h>     // LONG_MAX, INT_MAX
#include <sys/types.h>  // pid_t
#include <string.h>     // strdup, strchr, memset
#include <time.h>       // time, localtime_r, strftime, clock_gettime
#include <signal.h>     // sigaction, SIGINT, SIGTERM, sig_atomic_t
#include "proc.h"       // proc_read, read_system_uptime, struct proc_sample

struct config {
    pid_t *pids;
    size_t npids;
    long interval;
    long count;          // -1 = run forever
    const char *outfile; // NULL = stdout
};

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

static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s -p PID[,PID...] [-i SEC] [-n COUNT] [-o FILE] [-h]\n"
        "  -p PID[,PID...]  process IDs to monitor (required)\n"
        "  -i SEC           interval in seconds (default: 2)\n"
        "  -n COUNT         number of cycles (default: until stopped)\n"
        "  -o FILE          write output to FILE instead of stdout\n"
        "  -h               show this help\n", prog);
}

static int parse_long(const char *s, long min, long max, long *out)
{
    errno = 0;
    char *end;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || errno == ERANGE || v < min || v > max)
        return -1;

    *out = v;
    return 0;
}

static int parse_pids(const char *arg, struct config *cfg)
{
    char *copy = strdup(arg);
    if (!copy){
        perror("strdup");
        return -1;
    }

    size_t commas = 0;
    for (const char *p = copy; *p; p++)
        if (*p == ',')
            commas++;

    cfg->pids = malloc((commas + 1) * sizeof(pid_t));
    if (!cfg->pids) {
        perror("malloc");
        free(copy);
        return -1;
    }

    char *tok = copy;
    while (tok) {
        char *comma = strchr(tok, ',');
        if (comma)
            *comma = '\0';

        long v;
        if (parse_long(tok, 1, INT_MAX, &v) == 0) {
            cfg->pids[cfg->npids++] = (pid_t)v;
        } else if (tok[0] != '\0') {
            fprintf(stderr, "warning: invalid PID skipped: %s\n", tok);
        }

        if (!comma)
            break;
        tok = comma + 1;
    }

    free(copy);

    if (cfg->npids == 0) {
        fprintf(stderr, "No valid PIDs given\n");
        free(cfg->pids);
        cfg->pids = NULL;
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct config cfg = { NULL, 0, 2, -1, NULL };
    struct monitored *mons = NULL;
    FILE *outf = stdout;    
    int opt;
    int rc = 1; // return code

    while ((opt = getopt(argc, argv, "p:i:n:o:h")) != -1) {
        switch (opt) {
            case 'p':
                if (cfg.pids != NULL) {
                    fprintf(stderr, "option -p given more than once\n");
                    goto out;
                }
                if (parse_pids(optarg, &cfg) != 0) {
                    goto out;
                }
                break;
            case 'i':
                if (parse_long(optarg, 1, 86400, &cfg.interval) != 0) {
                    fprintf(stderr, "Invalid interval: %s\n", optarg);
                    goto out;
                }
                break;
            case 'n':
                if (parse_long(optarg, 1, LONG_MAX, &cfg.count) != 0) {
                    fprintf(stderr, "Invalid count: %s\n", optarg);
                    goto out;
                }
                break;
            case 'o':
                cfg.outfile = optarg;
                break;
            case 'h':
                print_usage(stdout, argv[0]);
                rc = 0; // help was requested, so exit code 0.
                goto out;
            default:
                print_usage(stderr, argv[0]);
                goto out;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument: %s\n", argv[0], argv[optind]);
        print_usage(stderr, argv[0]);
        goto out;
    }

    if (cfg.npids == 0) {
        fprintf(stderr, "Missing required option -p\n");
        print_usage(stderr, argv[0]);
        goto out;
    }

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

    /* print the header */
    fprintf(outf, "%-19s  %7s  %-15s  %6s  %11s  %s\n",
            "Timestamp", "PID", "Name", "CPU %", "Memory (MB)", "Uptime");

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
                /* same PID, different start time: the original process exited and the
                * kernel reused its PID for a new one */
                fprintf(stderr, "PID %d: process terminated (PID reused by another process), no longer monitored\n", (int)mons[i].pid);
                mons[i].alive = 0;
                continue;
            }
            mons[i].starttime = s.starttime;

            unsigned long long ticks = s.utime + s.stime;

            /* CPU % */
            char cpu_str[16];
            if (mons[i].have_prev && ticks >= mons[i].prev_ticks && now_mono > mons[i].prev_time) {
                double cpu = (ticks - mons[i].prev_ticks) / (double)clk / (now_mono - mons[i].prev_time) * 100.0;
                snprintf(cpu_str, sizeof cpu_str, "%6.1f", cpu);
            } else {
                snprintf(cpu_str, sizeof cpu_str, "%6s", "N/A");
            }

            mons[i].prev_ticks = ticks;
            mons[i].prev_time = now_mono;
            mons[i].have_prev = 1;

            /* uptime */
            long up = (long)(sys_up - s.starttime / (double)clk);
            if (up < 0)
                up = 0;
            long h = up / 3600;
            long m = (up % 3600) / 60;
            long sec = up % 60;
            char up_str[32];
            snprintf(up_str, sizeof up_str, "%02ld:%02ld:%02ld", h, m, sec);

            /* memory in MB */
            double mem_mb = s.rss_kb / 1024.0;

            /* print the row */
            fprintf(outf, "%-19s  %7d  %-15.15s  %s  %11.1f  %s\n",
                    tstr, (int)mons[i].pid, s.name, cpu_str, mem_mb, up_str);

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