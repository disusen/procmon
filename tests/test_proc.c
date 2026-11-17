/*
 * Unit tests for the /proc parsing in src/proc.c.
 *
 * Build and run with:  make test
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "proc.h"

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        tests_run++;                                                    \
        if (!(cond)) {                                                  \
            tests_failed++;                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

/* A realistic stat line: utime=12 (field 14), stime=5 (field 15),
 * starttime=183420 (field 22), followed by the remaining fields. */
#define STAT_TAIL " S 1 1234 1234 34816 1234 4194304 1000 0 0 0 12 5 0 0 20 0 1 0 183420 " \
                  "9465856 1280 18446744073709551615 1 1 0 0 0 0 65536 3686404 1266761467 " \
                  "0 0 0 17 3 0 0 0 0 0\n"

static void test_normal_line(void)
{
    struct proc_sample s;
    memset(&s, 0, sizeof s);

    CHECK(parse_stat_line("1234 (bash)" STAT_TAIL, &s) == 0);
    CHECK(strcmp(s.name, "bash") == 0);
    CHECK(s.utime == 12);
    CHECK(s.stime == 5);
    CHECK(s.starttime == 183420);
}

static void test_name_with_spaces_and_parens(void)
{
    struct proc_sample s;
    memset(&s, 0, sizeof s);

    /* Process names may contain spaces and ')' - the parser must use the
     * LAST ')' on the line, not the first one. */
    CHECK(parse_stat_line("42 (my )weird) app)" STAT_TAIL, &s) == 0);
    CHECK(strcmp(s.name, "my )weird) app") == 0);
    CHECK(s.utime == 12);
    CHECK(s.stime == 5);
    CHECK(s.starttime == 183420);
}

static void test_long_name_is_truncated(void)
{
    struct proc_sample s;
    char line[512];
    char longname[201];

    memset(&s, 0, sizeof s);
    memset(longname, 'a', 200);
    longname[200] = '\0';
    snprintf(line, sizeof line, "7 (%s)" STAT_TAIL, longname);

    CHECK(parse_stat_line(line, &s) == 0);
    CHECK(strlen(s.name) == sizeof s.name - 1);   /* truncated, still terminated */
    CHECK(s.utime == 12);
}

static void test_malformed_lines(void)
{
    struct proc_sample s;

    CHECK(parse_stat_line("", &s) == -1);
    CHECK(parse_stat_line("1234 bash S 1 2 3", &s) == -1);        /* no parentheses */
    CHECK(parse_stat_line("1234 (bash", &s) == -1);                /* no closing ')' */
    CHECK(parse_stat_line("1234 (bash) S 1 2 3\n", &s) == -1);     /* too few fields */
    CHECK(parse_stat_line("1234 (bash) S 1 1234 1234 34816 1234 4194304 1000 0 0 0 "
                          "abc 5 0 0 20 0 1 0 183420\n", &s) == -1); /* non-numeric utime */
}

static void test_read_own_process(void)
{
    struct proc_sample s;
    memset(&s, 0, sizeof s);

    CHECK(proc_read(getpid(), &s) == 0);
    CHECK(s.name[0] != '\0');   /* not "test_proc" exactly: under valgrind the name differs */
    CHECK(s.rss_kb > 0);
    CHECK(s.starttime > 0);
}

static void test_read_nonexistent_process(void)
{
    struct proc_sample s;

    /* INT_MAX is above the kernel's maximum PID, so it can never exist. */
    CHECK(proc_read((pid_t)INT_MAX, &s) == -1);
}

static void test_system_uptime(void)
{
    double up = -1.0;

    CHECK(read_system_uptime(&up) == 0);
    CHECK(up > 0.0);
}

int main(void)
{
    test_normal_line();
    test_name_with_spaces_and_parens();
    test_long_name_is_truncated();
    test_malformed_lines();
    test_read_own_process();
    test_read_nonexistent_process();
    test_system_uptime();

    printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}