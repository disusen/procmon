#include "proc.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>     // opendir, readdir, closedir
#include <stdlib.h>     // realloc, atoi
#include <ctype.h>      // isdigit

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

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>

/* Find all processes whose name (comm) equals name.
 * Appends their PIDs to the dynamic array *pids (with *count entries, *cap capacity),
 * growing it with realloc as needed. Returns the number of matches found, or -1 on error. */
int find_pids_by_name(const char *name, pid_t **pids, size_t *count, size_t *cap)
{
    DIR *d = opendir("/proc");
    if (!d)
        return -1;

    int matches = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        /* skip entries that aren't all digits */
        const char *n = e->d_name;
        int all_digits = 1;
        for (const char *p = n; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits || *n == '\0')
            continue;

        pid_t pid = (pid_t)atoi(n);

        struct proc_sample s;
        if (proc_read(pid, &s) != 0)
            continue;

        if (strcmp(s.name, name) == 0) {
            /* grow array if needed */
            if (*count == *cap) {
                size_t new_cap = (*cap == 0) ? 8 : *cap * 2;
                pid_t *tmp = realloc(*pids, new_cap * sizeof *tmp);
                if (!tmp) {
                    closedir(d);
                    return -1;
                }
                *pids = tmp;
                *cap = new_cap;
            }
            (*pids)[(*count)++] = pid;
            matches++;
        }
    }

    closedir(d);
    return matches;
}