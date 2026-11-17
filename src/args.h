#ifndef ARGS_H
#define ARGS_H

#include <stdio.h>
#include <sys/types.h>

/* Settings from the command line */
struct config {
    pid_t *pids;         // PIDs to monitor (from -p and -N), malloc'd, caller frees
    size_t npids;
    long interval;       // seconds between cycles
    long count;          // -1 = run forever
    const char *outfile; // NULL = stdout
    int csv;             // 1 = CSV output, 0 = table
    const char *names;   // -N argument: comma-separated process names, NULL if not given
};

/* Return values of parse_args */
#define ARGS_OK     0    /* continue with monitoring */
#define ARGS_EXIT   1    /* nothing to do, exit successfully (-h was given) */
#define ARGS_ERROR -1    /* invalid arguments; the error has already been printed */

/* Parse the command line into *cfg: options, -N name lookup, duplicate removal.
 * On ARGS_ERROR, cfg->pids may still be allocated: the caller always frees it. */
int parse_args(int argc, char *argv[], struct config *cfg);

void print_usage(FILE *out, const char *prog);

#endif