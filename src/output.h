#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdio.h>
#include <sys/types.h>

/* Print the column header, as a table or as CSV. */
void print_header(FILE *out, int csv);

/* Print one measurement row, as a table or as CSV.
 * cpu_valid = 0 means there is no CPU value yet (shown as N/A / empty field). */
void print_row(FILE *out, int csv, const char *timestamp, pid_t pid, const char *name,
               int cpu_valid, double cpu, double mem_mb, long uptime_s);

#endif