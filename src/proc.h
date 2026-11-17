#ifndef PROC_H
#define PROC_H

#include <sys/types.h>

/* One snapshot of a process, read from /proc */
struct proc_sample {
    char name[64];
    unsigned long long utime;      /* field 14: user-mode CPU time, clock ticks */
    unsigned long long stime;      /* field 15: kernel-mode CPU time, clock ticks */
    unsigned long long starttime;  /* field 22: start time, ticks since boot */
    long rss_kb;                   /* VmRSS from /proc/<pid>/status, 0 if absent */
};

/* Parse one line of /proc/<pid>/stat into s (name, utime, stime, starttime).
 * Pure string parsing, no file access. Returns 0 on success, -1 on malformed input. */
int parse_stat_line(const char *line, struct proc_sample *s);

/* Read /proc/<pid>/stat and /proc/<pid>/status into s.
 * Returns 0 on success, -1 if the process doesn't exist or can't be read. */
int proc_read(pid_t pid, struct proc_sample *s);

/* Seconds since boot, from /proc/uptime. Returns 0 on success, -1 on error. */
int read_system_uptime(double *out);

/* Find all processes whose name (comm) equals `name`.
 * Appends their PIDs to the dynamic array *pids (with *count entries, *cap capacity),
 * growing it with realloc as needed. Returns the number of matches found, or -1 on error. */
int find_pids_by_name(const char *name, pid_t **pids, size_t *count, size_t *cap);

#endif