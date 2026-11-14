#include <stdio.h>
#include <unistd.h>

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

int main(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "p:i:n:o:h")) != -1) {
        switch (opt) {
            case 'p':
                printf("option p: %s\n", optarg);
                break;
            case 'i':
                printf("option i: %s\n", optarg);
                break;
            case 'n':
                printf("option n: %s\n", optarg);
                break;
            case 'o':
                printf("option o: %s\n", optarg);
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

    return 0;
}