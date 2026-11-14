#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>     // strtol
#include <errno.h>      // errno, ERANGE
#include <limits.h>     // LONG_MAX
#include <sys/types.h>  // pid_t

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

int main(int argc, char *argv[])
{
    struct config cfg = { NULL, 0, 2, -1, NULL };
    int opt;

    while ((opt = getopt(argc, argv, "p:i:n:o:h")) != -1) {
        switch (opt) {
            case 'p':
                printf("option p: %s\n", optarg);
                break;
            case 'i':
                if (parse_long(optarg, 1, 86400, &cfg.interval) != 0) {
                    fprintf(stderr, "Invalid interval: %s\n", optarg);
                    return 1;
                }
                break;
            case 'n':
                if (parse_long(optarg, 1, LONG_MAX, &cfg.count) != 0) {
                    fprintf(stderr, "Invalid count: %s\n", optarg);
                    return 1;
                }
                break;
            case 'o':
                cfg.outfile = optarg;
                break;
            case 'h':
                print_usage(stdout, argv[0]);
                return 0;
            default:
                print_usage(stderr, argv[0]);
                return 1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument: %s\n", argv[0], argv[optind]);
        print_usage(stderr, argv[0]);
        return 1;
    }

    printf("interval=%ld count=%ld outfile=%s\n",
           cfg.interval, cfg.count, cfg.outfile ? cfg.outfile : "(stdout)");

    return 0;
}