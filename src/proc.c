#include "proc.h"
#include <stdio.h>
#include <string.h>

int parse_stat_line(const char *line, struct proc_sample *s)
{
    if (!line || !s)
        return -1;

    const char *open = strchr(line, '(');    /* first '(' */
    const char *close = strrchr(line, ')');  /* LAST ')' */
    if (!open || !close || close < open)
        return -1;

    /* Copy the name: length is close - open - 1. Cap at sizeof(s->name)-1. */
    size_t len = (size_t)(close - open - 1);
    if (len > sizeof(s->name) - 1)
        len = sizeof(s->name) - 1;
    memcpy(s->name, open + 1, len);
    s->name[len] = '\0';

    if (close[1] != ' ')
        return -1;


    /* Parse the numbers that start after ") " */
    int n = sscanf(close + 2,
        "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %*d %*d %llu",
        &s->utime, &s->stime, &s->starttime);
    if (n != 3)
        return -1;
    return 0;
}

int proc_read(pid_t pid, struct proc_sample *s)
{
    if (!s)
        return -1;

    char path[64];
    char buf[1024];

    /* Read /proc/<pid>/stat */
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return -1;

    if (fgets(buf, sizeof buf, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (parse_stat_line(buf, s) != 0)
        return -1;

    /* Read /proc/<pid>/status for memory */
    s->rss_kb = 0;  /* kernel threads have no VmRSS line */
    snprintf(path, sizeof path, "/proc/%d/status", (int)pid);
    f = fopen(path, "r");
    if (f == NULL)
        return -1;

    while (fgets(buf, sizeof buf, f)) {
        if (strncmp(buf, "VmRSS:", 6) == 0) {
            sscanf(buf + 6, "%ld", &s->rss_kb);
            break;
        }
    }
    fclose(f);
    return 0;
}

int read_system_uptime(double *out)
{
    if (!out)
        return -1;

    FILE *f = fopen("/proc/uptime", "r");
    if (f == NULL)
        return -1;

    int r = fscanf(f, "%lf", out);
    fclose(f);
    return (r == 1) ? 0 : -1;
}