#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>     // strtol, malloc, free, strdup
#include <errno.h>      // errno, ERANGE
#include <limits.h>     // LONG_MAX, INT_MAX
#include <sys/types.h>  // pid_t
#include <string.h>     // strdup, strchr

struct config {
    pid_t *pids;
    size_t npids;
    long interval;
    long count;          // -1 = run forever
    const char *outfile; // NULL = stdout
};

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
    
    if (cfg->npids==0)
    {
        fprintf(stderr, "No valid PIDs given\n");
        free(cfg->pids);
        cfg->pids = NULL;
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
        free(cfg->pids);
        cfg->pids = NULL;
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct config cfg = { NULL, 0, 2, -1, NULL };
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

    printf("pids=");
    for (size_t i = 0; i < cfg.npids; i++)
        printf("%s%d", i ? "," : "", (int)cfg.pids[i]);
    printf(" interval=%ld count=%ld outfile=%s\n",
           cfg.interval, cfg.count, cfg.outfile ? cfg.outfile : "(stdout)");
    rc = 0; // reached the end, success, set exit code to 0.

out:
    free(cfg.pids);
    return rc;
}