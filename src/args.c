#include "args.h"
#include "proc.h"       // find_pids_by_name
#include <stdio.h>      // fprintf, perror
#include <stdlib.h>     // strtol, malloc, free
#include <string.h>     // strdup, strchr, strlen
#include <errno.h>      // errno, ERANGE
#include <limits.h>     // LONG_MAX, INT_MAX
#include <unistd.h>     // getopt, optarg, optind

void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [-p PID[,PID...]] [-N NAME[,NAME...]] [-i SEC] [-n COUNT] [-o FILE] [-c] [-h]\n"
        "  At least one of -p or -N is required.\n"
        "  -p PID[,PID...]    process IDs to monitor\n"
        "  -N NAME[,NAME...]  monitor all processes with these names (as shown by ps, max 15 chars)\n"
        "  -i SEC             interval in seconds (default: 2)\n"
        "  -n COUNT           number of cycles (default: until stopped)\n"
        "  -o FILE            write output to FILE instead of stdout\n"
        "  -c                 CSV output instead of a table\n"
        "  -h                 show this help\n", prog);
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

/* For each comma-separated name in arg, find matching processes and append
 * their PIDs to cfg->pids. Returns 0 on success, -1 on error (allocation). */
static int add_pids_by_names(const char *arg, struct config *cfg)
{
    /* copy, because the string is cut at the commas (same as parse_pids) */
    char *copy = strdup(arg);
    if (!copy) {
        perror("strdup");
        return -1;
    }

    /* The array may already contain PIDs from -p. find_pids_by_name needs to know
     * its capacity; parse_pids allocated exactly npids entries, so: */
    size_t cap = cfg->npids;

    char *tok = copy;
    while (tok) {
        char *comma = strchr(tok, ',');
        if (comma)
            *comma = '\0';

        if (tok[0] != '\0') {
            if (strlen(tok) > 15)
                fprintf(stderr, "warning: process names are at most 15 characters: '%s'\n", tok);

            int found = find_pids_by_name(tok, &cfg->pids, &cfg->npids, &cap);
            if (found < 0) {
                perror("find_pids_by_name");
                free(copy);
                return -1;
            }
            if (found == 0)
                fprintf(stderr, "warning: no process named '%s'\n", tok);
        }

        tok = comma ? comma + 1 : NULL;
    }

    free(copy);
    return 0;
}

int parse_args(int argc, char *argv[], struct config *cfg)
{
    /* defaults */
    *cfg = (struct config){ NULL, 0, 2, -1, NULL, 0, NULL };

    int opt;
    while ((opt = getopt(argc, argv, "p:i:n:o:hcN:")) != -1) {
        switch (opt) {
            case 'p':
                if (cfg->pids != NULL) {
                    fprintf(stderr, "option -p given more than once\n");
                    return ARGS_ERROR;
                }
                if (parse_pids(optarg, cfg) != 0)
                    return ARGS_ERROR;
                break;
            case 'i':
                if (parse_long(optarg, 1, 86400, &cfg->interval) != 0) {
                    fprintf(stderr, "Invalid interval: %s\n", optarg);
                    return ARGS_ERROR;
                }
                break;
            case 'n':
                if (parse_long(optarg, 1, LONG_MAX, &cfg->count) != 0) {
                    fprintf(stderr, "Invalid count: %s\n", optarg);
                    return ARGS_ERROR;
                }
                break;
            case 'o':
                cfg->outfile = optarg;
                break;
            case 'h':
                print_usage(stdout, argv[0]);
                return ARGS_EXIT;          /* help was requested: exit code 0 */
            case 'c':
                cfg->csv = 1;
                break;
            case 'N':
                cfg->names = optarg;       /* resolved after option parsing */
                break;
            default:
                print_usage(stderr, argv[0]);
                return ARGS_ERROR;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument: %s\n", argv[0], argv[optind]);
        print_usage(stderr, argv[0]);
        return ARGS_ERROR;
    }

    if (cfg->names && add_pids_by_names(cfg->names, cfg) != 0)
        return ARGS_ERROR;

    /* Remove duplicate PIDs (e.g. given with -p and also found by -N).
     * k = number of unique PIDs kept so far; each PID is kept only if
     * it isn't already among the first k. */
    size_t k = 0;
    for (size_t i = 0; i < cfg->npids; i++) {
        size_t j = 0;
        while (j < k && cfg->pids[j] != cfg->pids[i])
            j++;
        if (j == k)                      /* not seen before */
            cfg->pids[k++] = cfg->pids[i];
    }
    cfg->npids = k;

    if (cfg->npids == 0) {
        if (cfg->names) {
            fprintf(stderr, "No matching processes found to monitor\n");
        } else {
            fprintf(stderr, "Missing required option -p or -N\n");
            print_usage(stderr, argv[0]);
        }
        return ARGS_ERROR;
    }

    return ARGS_OK;
}