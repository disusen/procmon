#include "output.h"
#include <stdio.h>      // fprintf, fputs, fputc, snprintf
#include <string.h>     // strpbrk

/* Write s as one CSV field (RFC 4180): fields containing , " or a newline
 * are wrapped in quotes, and quotes inside are doubled. */
static void fprint_csv_field(FILE *f, const char *s)
{
    if (strpbrk(s, ",\"\n") == NULL) {
        fputs(s, f);
        return;
    }

    fputc('"', f);
    for (const char *p = s; *p; p++) {
        fputc(*p, f);
        if (*p == '"')
            fputc('"', f);
    }
    fputc('"', f);
}

void print_header(FILE *out, int csv)
{
    if (csv)
        fprintf(out, "timestamp,pid,name,cpu_percent,memory_mb,uptime_s\n");
    else
        fprintf(out, "%-19s  %7s  %-15s  %6s  %11s  %s\n",
                "Timestamp", "PID", "Name", "CPU %", "Memory (MB)", "Uptime");
}

void print_row(FILE *out, int csv, const char *timestamp, pid_t pid, const char *name,
               int cpu_valid, double cpu, double mem_mb, long uptime_s)
{
    if (csv) {
        fprintf(out, "%s,%d,", timestamp, (int)pid);
        fprint_csv_field(out, name);
        if (cpu_valid)
            fprintf(out, ",%.1f", cpu);
        else
            fputs(",", out);          /* empty field: no CPU value yet */
        fprintf(out, ",%.1f,%ld\n", mem_mb, uptime_s);
        return;
    }

    char cpu_str[16];
    if (cpu_valid)
        snprintf(cpu_str, sizeof cpu_str, "%6.1f", cpu);
    else
        snprintf(cpu_str, sizeof cpu_str, "%6s", "N/A");

    /* uptime as HH:MM:SS (hours may exceed 24) */
    char up_str[32];
    snprintf(up_str, sizeof up_str, "%02ld:%02ld:%02ld",
             uptime_s / 3600, (uptime_s % 3600) / 60, uptime_s % 60);

    fprintf(out, "%-19s  %7d  %-15.15s  %s  %11.1f  %s\n",
            timestamp, (int)pid, name, cpu_str, mem_mb, up_str);
}