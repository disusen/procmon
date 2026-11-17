# procmon: Linux Process Monitor

`procmon` is a command-line tool written in C that monitors one or more running
Linux processes and periodically reports their resource usage: CPU usage,
memory (RSS) and uptime. All data is read directly from the `/proc`
filesystem. The tool does not run external commands such as `ps` or `top`.

```
$ ./procmon -p 1,6808 -i 1 -n 3
Timestamp                PID  Name              CPU %  Memory (MB)  Uptime
2026-11-16 20:38:13        1  systemd             N/A         16.5  02:00:44
2026-11-16 20:38:13     6808  sleep               N/A          8.0  00:00:00
2026-11-16 20:38:14        1  systemd             0.0         16.5  02:00:45
2026-11-16 20:38:14     6808  sleep               0.0          8.0  00:00:01
2026-11-16 20:38:15        1  systemd             0.0         16.5  02:00:46
2026-11-16 20:38:15     6808  sleep               0.0          8.0  00:00:02
```

## Contents

- [Features](#features)
- [Requirements and dependencies](#requirements-and-dependencies)
- [Building](#building)
- [Usage](#usage)
- [Command-line arguments](#command-line-arguments)
- [Output format](#output-format)
- [How the values are calculated](#how-the-values-are-calculated)
- [Error handling](#error-handling)
- [Running as a systemd service](#running-as-a-systemd-service)
- [Testing](#testing)
- [Known limitations](#known-limitations)
- [Project structure](#project-structure)

## Features

- Monitors several processes at once, by PID (`-p 1234,2345`) and/or by name
  (`-N sshd,nginx`)
- Configurable interval (`-i`) and number of cycles (`-n`), or runs until stopped
- Reports PID, process name, CPU %, resident memory (RSS), process uptime and a
  timestamp for every measurement
- Writes to stdout or to a file (`-o`), as a readable table or as CSV (`-c`)
- Keeps monitoring the remaining processes when one of them exits or cannot be
  read
- Shuts down gracefully on `SIGINT` (Ctrl+C) and `SIGTERM` (`systemctl stop`):
  it stops the loop, closes the output file and frees all memory
- Ships with a systemd service that runs as an unprivileged user
- Unit tests for the `/proc` parsing and a script that runs all required test
  scenarios

## Requirements and dependencies

- Linux with the `/proc` filesystem
- GCC and GNU Make (on Debian/Ubuntu: `sudo apt install build-essential`)
- Optional: `valgrind`, for memory checking
- Optional: systemd, to run procmon as a service

The code uses only the C standard library and POSIX APIs (`getopt`,
`sigaction`, `clock_gettime`, `localtime_r`, `sysconf`). There are no
third-party libraries.

Developed and tested on Ubuntu Server (x86_64) running in VirtualBox.

## Building

```bash
git clone https://github.com/disusen/procmon.git
cd procmon
make            # builds ./procmon
make test       # builds and runs the unit tests
make clean      # removes all build artifacts
```

Everything is compiled with `-Wall -Wextra -Werror -std=c11`, so any compiler
warning fails the build.

| Target | Description |
|---|---|
| `all` (default) | Build the `procmon` executable |
| `test` | Build and run the unit tests in `tests/test_proc.c` |
| `clean` | Remove the executable, object files and dependency files |

## Usage

```bash
# Monitor one process every 2 seconds (default) until Ctrl+C
./procmon -p 1234

# Monitor three processes every 2 seconds, 10 cycles, write to a file
./procmon -p 1234,2345,3456 -i 2 -n 10 -o process.log

# Monitor your current shell once per second, 5 times
./procmon -p $$ -i 1 -n 5

# Monitor all processes named sshd or bash
./procmon -N sshd,bash -i 5

# Combine PIDs and names (a PID matched by both is monitored once)
./procmon -p 1 -N sshd -n 10

# CSV output into a file, e.g. for a spreadsheet
./procmon -p 1234,2345 -i 5 -c -o process.csv

# Show help
./procmon -h
```

Useful ways to find PIDs to monitor: `pgrep <name>`, `echo $$` (the current
shell), or `sleep 300 & echo $!` (a test process).

## Command-line arguments

| Argument | Description |
|---|---|
| `-p PID[,PID...]` | One or more process IDs, separated by commas (no spaces). At least one of `-p` / `-N` is required. |
| `-N NAME[,NAME...]` | Monitor all processes with these names (see [Monitoring by name](#monitoring-by-name--n)). Can be combined with `-p`. |
| `-i SEC` | Monitoring interval in seconds, from 1 to 86400. Default: `2`. |
| `-n COUNT` | Number of monitoring cycles, at least 1. If omitted, procmon runs until stopped. |
| `-o FILE` | Write measurements to `FILE` instead of stdout. The file is opened in append mode. |
| `-c` | Output CSV instead of a table (see [CSV output](#csv-output--c)). |
| `-h` | Show help and exit. |

Notes:

- `-n` counts monitoring cycles, not rows. Each cycle produces one row for
  every process that is still being monitored.
- Numbers are parsed strictly with `strtol`. Values like `abc`, `2x`, `5.5`,
  `0`, negative numbers and values that overflow are rejected with an error
  message and exit code 1.
- Invalid entries in the PID list (for example `abc`, `0` or empty entries in
  `12,,34`) are skipped with a warning, so they don't prevent valid PIDs from
  being monitored. procmon exits with an error only if no valid PID is left.
- Giving `-p` twice, passing unknown options or passing extra arguments is an
  error. Giving neither `-p` nor `-N` is an error too.

**Exit codes:** `0` means a normal finish, a stop by `SIGINT`/`SIGTERM`, or
that all monitored processes exited. `1` means invalid arguments, an output
file error, no monitorable PIDs, or another fatal error.

## Monitoring by name (`-N`)

`-N` takes one or more process names, separated by commas. At startup, procmon
scans `/proc` (every directory whose name is a number is a process), reads
each process's name and collects the PIDs of all processes whose name matches
exactly. Those PIDs are then monitored exactly as if they had been given with
`-p`.

- The name is the kernel's `comm` value, which is what `ps -o comm` and
  `pgrep -x` use. The kernel cuts it to **15 characters**, so give the name as
  `ps` shows it. procmon warns if a longer name is given.
- All matching processes are monitored. For example, `-N bash` finds every
  bash shell.
- An unknown name produces a warning. procmon exits with an error only if, in
  total, nothing is left to monitor.
- If the same PID is given with `-p` and also matched by `-N`, it is
  monitored only once.
- Names are resolved **once, at startup**. Processes with that name that start
  later are not added (see [Known limitations](#known-limitations)).

## Output format

The output is a plain-text table with a header line and one row per process
per cycle:

| Column | Meaning |
|---|---|
| Timestamp | Local time of the measurement (`YYYY-MM-DD HH:MM:SS`) |
| PID | Process ID |
| Name | Process name (the kernel's `comm` field, max. 15 characters) |
| CPU % | CPU usage since the previous cycle; `N/A` on the first measurement |
| Memory (MB) | Resident Set Size (RSS) in MiB |
| Uptime | Time since the process started, `HH:MM:SS` (hours may exceed 24) |

Measurements go to stdout (or the `-o` file). Warnings and errors, such as
"process terminated", go to **stderr**, so the measurement output stays clean
and machine-readable. When writing to a file, procmon flushes it after every
cycle, so `tail -f` shows new rows immediately.

### CSV output (`-c`)

With `-c` the same measurements are written as CSV, for use in spreadsheets or
scripts. This works for both stdout and `-o`:

```
$ ./procmon -p 1,6808 -i 1 -n 2 -c
timestamp,pid,name,cpu_percent,memory_mb,uptime_s
2026-11-17 18:26:35,1,systemd,,16.5,7265
2026-11-17 18:26:35,6808,sleep,,8.0,0
2026-11-17 18:26:36,1,systemd,0.0,16.5,7266
2026-11-17 18:26:36,6808,sleep,0.0,8.0,1
```

The format is chosen to be easy to process:

- `cpu_percent` is **empty** (not `N/A`) when no value is available yet, so
  tools read it as a missing number rather than text.
- `uptime_s` is the uptime in whole **seconds** instead of `HH:MM:SS`.
- No padding, and the full process name is written (not truncated).
- Fields are quoted according to RFC 4180: a name that contains a comma, a
  double quote or a newline is wrapped in `"..."`, with inner quotes doubled
  (`a,b` becomes `"a,b"`).

## How the values are calculated

All values come from three files:

| File | Fields used |
|---|---|
| `/proc/<pid>/stat` | 2 `comm` (name), 14 `utime`, 15 `stime`, 22 `starttime` |
| `/proc/<pid>/status` | `VmRSS` line (resident memory in kB) |
| `/proc/uptime` | first value: seconds since boot |

### CPU usage

`utime` and `stime` are the total CPU time the process has used since it
started, in user mode and kernel mode, measured in clock ticks.
`sysconf(_SC_CLK_TCK)` gives the number of ticks per second (usually 100).
These are cumulative counters, so CPU usage is calculated from the difference
between two consecutive measurements:

```
                 (ticks_now - ticks_prev) / CLK_TCK
CPU % = 100  ×  ────────────────────────────────────
                       time_now - time_prev

where ticks = utime + stime
```

- The numerator is the number of CPU-seconds the process used during the
  interval. The denominator is the number of real seconds that passed.
- The real time comes from `clock_gettime(CLOCK_MONOTONIC)`. Unlike wall-clock
  time, it never jumps when the system clock is adjusted, and it has sub-second
  precision. It measures the interval that actually passed, not the requested
  `-i`, so delays in the program don't distort the result.
- **First measurement:** with only one sample there is nothing to compare
  against, so CPU % is shown as `N/A` instead of a misleading value. Averaging
  over the whole process lifetime would hide current activity.
- **Multi-core:** the value is relative to one CPU core, like `top` reports it.
  A multithreaded process can therefore show more than 100% (for example 200%
  when it fully uses two cores).
- If the tick counter goes backwards (which should not happen for the same
  process) or no time has passed, `N/A` is shown instead of a negative or
  infinite value.

### Memory

The `VmRSS` value from `/proc/<pid>/status`, converted from kB to MB
(divided by 1024). RSS is the amount of the process's memory that is
currently held in RAM. Kernel threads have no `VmRSS` line and are shown with
0.0.

### Uptime

```
uptime = system_uptime - starttime / CLK_TCK
```

`starttime` is the moment the process started, in clock ticks after boot. The
result is formatted as `HH:MM:SS`.

### Parsing `/proc/<pid>/stat`

The process name (field 2) is in parentheses and may itself contain spaces or
`)`, for example `(my )app)`. Splitting the line on spaces would therefore
break. The parser looks for the **last** `)` on the line, takes everything
between the first `(` and that `)` as the name, and parses the numeric fields
after it with `sscanf`. Malformed lines are rejected instead of producing
garbage values. These cases are covered by the unit tests.

## Error handling

| Situation | Behavior |
|---|---|
| Invalid or missing arguments | Error message and usage on stderr, exit code 1 |
| Invalid PID syntax in `-p` | Warning, PID skipped; exit 1 only if no valid PID is left |
| No process matches a `-N` name | `warning: no process named 'X'`; exit 1 only if nothing is left to monitor |
| Neither `-p` nor `-N` given | `Missing required option -p or -N` + usage, exit 1 |
| PID does not exist | `PID N: no such process`; other PIDs keep being monitored |
| `/proc` entry not readable | `PID N: permission denied`; other PIDs keep being monitored |
| Process exits during monitoring | `PID N: process terminated, no longer monitored`; others continue |
| PID reused by a new process between cycles | Detected via `starttime`; reported as terminated, no longer monitored |
| All monitored processes have exited | Message, then a clean exit with code 0 |
| None of the PIDs could be read at start | Message, exit code 1 |
| Output file cannot be created/opened | e.g. `out.log: Permission denied`, exit code 1 |
| Write error during monitoring (e.g. disk full) | Detected through `fflush`/`ferror`; error message, exit code 1 |
| Memory allocation failure | Reported with `perror`, exit code 1 |
| `/proc/uptime` or clock unavailable | Error message, exit code 1 |
| `SIGINT` / `SIGTERM` | Graceful shutdown (see below) |

All of these paths go through a single cleanup section at the end of `main`,
which closes the output file and frees all memory. This means no path leaks
resources, regardless of where an error happens or in which order the options
were given. This was checked with valgrind (see [Testing](#testing)).

### Signal handling

A handler for `SIGINT` and `SIGTERM` is installed with `sigaction`. The handler
only sets a `volatile sig_atomic_t` flag, because functions like `printf`,
`fclose` or `free` are not safe to call inside a signal handler. The main loop
checks the flag on every cycle. A signal also interrupts `sleep()`, so procmon
reacts immediately and does not wait for the rest of the interval. After the
loop it prints a short termination message, closes the output file, frees
memory and exits with code 0.

## Running as a systemd service

The `systemd/` directory contains:

- `procmon.service`: the unit file
- `procmon.conf`: configuration (what to monitor and the interval)

The service runs as a dedicated unprivileged system user `procmon`, writes its
measurements to `/var/log/procmon/procmon.log` and starts automatically at
boot.

### 1. Create the user and install the files

```bash
make

# Dedicated system user: no login shell, no home directory
sudo useradd --system --no-create-home --shell /usr/sbin/nologin procmon

# Binary, configuration and unit file
sudo install -m 755 procmon /usr/local/bin/procmon
sudo install -d /etc/procmon
sudo install -m 644 systemd/procmon.conf /etc/procmon/procmon.conf
sudo install -m 644 systemd/procmon.service /etc/systemd/system/procmon.service

sudo systemctl daemon-reload
```

### 2. Configure

Edit `/etc/procmon/procmon.conf`:

```ini
# What to monitor: -N name[,name...] and/or -p pid[,pid...]
PROCMON_TARGET=-N systemd,sshd

# Monitoring interval in seconds
PROCMON_INTERVAL=5
```

The unit file reads these values through `EnvironmentFile=` and passes them to
`procmon`, so the configuration can be changed without editing the unit. Apply
changes with `sudo systemctl restart procmon`.

`PROCMON_TARGET` is used as `$PROCMON_TARGET` (without braces) in
`ExecStart=`. systemd splits such a variable at whitespace into separate
arguments, so the same setting can hold `-N …`, `-p …` or both, for example
`PROCMON_TARGET=-N sshd -p 1`.

Monitoring **by name** is recommended for the service. Names are looked up
each time the service starts, so the service keeps working after a reboot,
when the PIDs have changed. The default monitors `systemd` and `sshd`.

### 3. Start and enable at boot

```bash
sudo systemctl enable --now procmon     # start now and at every boot
```

### 4. Check status

```bash
systemctl status procmon
systemctl is-enabled procmon            # "enabled" = starts at boot
ps -o user,pid,cmd -C procmon           # confirms it runs as user "procmon"
```

### 5. View logs

```bash
# Measurements
sudo tail -f /var/log/procmon/procmon.log

# Service events and procmon's warnings/errors (stderr goes to the journal)
journalctl -u procmon
journalctl -u procmon -f                # follow live
```

### 6. Stop and disable

```bash
sudo systemctl stop procmon             # sends SIGTERM -> graceful shutdown
sudo systemctl disable procmon          # don't start at boot anymore
```

To uninstall completely:

```bash
sudo systemctl disable --now procmon
sudo rm /etc/systemd/system/procmon.service /usr/local/bin/procmon
sudo rm -r /etc/procmon /var/log/procmon
sudo systemctl daemon-reload
sudo userdel procmon
```

### Unit file explained

| Setting | Purpose |
|---|---|
| `User=procmon`, `Group=procmon` | Run without root privileges |
| `EnvironmentFile=/etc/procmon/procmon.conf` | Load `PROCMON_TARGET` and `PROCMON_INTERVAL` |
| `LogsDirectory=procmon` | systemd creates `/var/log/procmon` owned by the service user |
| `ExecStart=... -o /var/log/procmon/procmon.log` | Write measurements to the log file |
| `KillSignal=SIGTERM` | `systemctl stop` sends SIGTERM, which procmon handles gracefully |
| `Restart=on-failure`, `RestartSec=10` | Restart after an error exit. A normal exit (e.g. all processes ended) is not restarted. |
| `StartLimitIntervalSec=60`, `StartLimitBurst=3` | Stop retrying after 3 failures in 60 s (e.g. nothing to monitor) |
| `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp` | Basic hardening: the service can only read `/proc` and write its log directory |
| `WantedBy=multi-user.target` | Started automatically at boot once enabled |

## Testing

### Unit tests

```bash
make test
```

`tests/test_proc.c` tests the `/proc` functions: 31 checks covering normal
stat lines, process names containing spaces and `)`, truncation of overly long
names, malformed lines (missing parentheses, too few fields, non-numeric
values), reading the test's own process, a non-existent PID, `/proc/uptime`,
and finding processes by name (the test finds itself by its own name; an
unknown name finds nothing).

```
31 checks, 0 failed
```

### Required test scenarios

All 10 scenarios from the assignment are automated in `tests/run_scenarios.sh`.
The script starts helper processes (`sleep`, a busy loop), runs procmon
against them and prints each command, its complete output and its exit code.
Run it from the repository root as a normal (non-root) user:

```bash
./tests/run_scenarios.sh                          # print results
./tests/run_scenarios.sh | tee tests/results.txt  # print and save
VALGRIND=1 ./tests/run_scenarios.sh               # same, under valgrind
```

The complete recorded output is in [`tests/results.txt`](tests/results.txt).
Summary:

| # | Scenario | How it was tested | Result |
|---|---|---|---|
| 1 | Single valid PID | `sleep 60` process, `-i 1 -n 3` | 3 rows; CPU `N/A`, then 0.0; uptime increasing. Exit 0 ✅ |
| 2 | Multiple valid PIDs | `sleep` + busy loop, `-i 1 -n 3` | Rows for both PIDs each cycle; busy loop ~95% CPU, sleep 0.0%. Exit 0 ✅ |
| 3 | Non-existent PID | PID above `pid_max`; also mixed with a valid PID; also `abc,…,,0` | `no such process`; alone → exit 1; mixed → valid PID still monitored, exit 0; invalid syntax skipped with warnings ✅ |
| 4 | Process terminates during monitoring | `sleep 2`, `-i 1 -n 5` | Rows while alive, then `process terminated`, `All monitored processes have exited`. Exit 0 ✅ |
| 5 | Invalid interval | `-i 0`, `-3`, `abc`, `2x`, `99999999999999999999` | All rejected with `Invalid interval: …`. Exit 1 ✅ |
| 6 | Invalid count | `-n 0`, `-1`, `xyz`, `5.5` | All rejected with `Invalid count: …`. Exit 1 ✅ |
| 7 | Ctrl+C | SIGINT sent after ~2.5 s (also tested with SIGTERM, and manually with Ctrl+C in a terminal) | Loop stops immediately, `Stop signal received, exiting.` Exit 0 ✅ |
| 8 | Output to file | `-o process.log`, then `cat` | Nothing on stdout; header + rows in the file ✅ |
| 9 | Inaccessible output file | Non-existent directory, `/root/…` as a normal user, read-only file | `No such file or directory` / `Permission denied`. Exit 1 ✅ |
| 10 | Multiple PIDs, one terminates | `sleep 60` + `sleep 2`, `-i 1 -n 5` | `process terminated` for the short one; the other keeps being monitored for all 5 cycles. Exit 0 ✅ |

### Optional feature: CSV output

The scenario script also tests `-c` (end of [`tests/results.txt`](tests/results.txt)):
CSV output for several processes, an empty `cpu_percent` on the first cycle,
correct quoting of a process whose name contains a comma (`"a,b"`), and that
the output parses back into 6 fields per row with Python's `csv` module.

### Optional feature: monitoring by name

Also covered by the scenario script (end of `tests/results.txt`). Two helper
processes are renamed to `pm_target`, and the script checks the following:
`-N pm_target` finds and monitors both; combined with `-p` (one of the two PIDs
plus PID 1), the PID matched twice is monitored only once; an unknown name next
to a valid one gives a warning while monitoring continues; only unknown names
exit with code 1; and neither `-p` nor `-N` is rejected.

### Memory checks

All scenarios were also run under valgrind
(`VALGRIND=1 ./tests/run_scenarios.sh`, which uses
`valgrind --leak-check=full --error-exitcode=99`). Valgrind reported no leaks
and no invalid memory accesses on any path, including the error exits and the
signal-triggered shutdown. The unit tests are also valgrind-clean.

### systemd service

The service was tested manually on the VM following the steps in
[Running as a systemd service](#running-as-a-systemd-service):

- `systemctl status procmon` showed `active (running)` and `enabled`
- `ps` confirmed the process runs as user `procmon`, not root
- `/var/log/procmon/procmon.log` received a new row every 5 seconds
- After changing the monitored processes in the config file and restarting,
  the log showed rows for the new processes
- `systemctl stop procmon` produced `Stop signal received, exiting.` in
  `journalctl -u procmon`, followed by `Deactivated successfully`, which
  confirms the SIGTERM handler and cleanup ran

## Known limitations

- **CPU % on the first cycle** is always `N/A`, because a rate needs two
  samples.
- **CPU % precision** is limited by the clock tick resolution (usually 10 ms).
  With short intervals and processes that use little CPU, values are coarse.
  The interval is at least 1 second.
- **CPU % is per core**, so it can exceed 100% for multithreaded processes on
  multi-core systems. This is the same convention `top` uses.
- **PID reuse** is detected by comparing each process's `starttime` between
  cycles. If it changes, the original process has exited and its PID now
  belongs to a different process, so it is reported as terminated. This case
  can't be triggered on demand (the kernel cycles through all PIDs before
  reusing one), so it is not part of the automated test scenarios.
- **The interval is approximate:** procmon sleeps `-i` seconds after each
  cycle, so the real period is slightly longer than `-i` (by the time it takes
  to read `/proc`). This does not affect CPU %, which uses the actually
  measured time.
- **Names (`-N`) are resolved once at startup.** Processes with a matching
  name that start later are not picked up, and a restarted process (new PID)
  is reported as terminated. For long-running use, restart procmon (or the
  service) to pick up new processes.
- **PIDs (`-p`) are not stable across reboots.** For the systemd service,
  monitoring by name avoids this, because names are looked up on every
  service start.
- **Process names** come from the kernel's `comm` field, which is limited to
  15 characters. Longer program names appear truncated (the same as in `ps`),
  and `-N` must use the truncated name.
- **Restricted `/proc`:** if `/proc` is mounted with `hidepid`, processes of
  other users are not visible to a non-root user and are reported as not
  existing.
- **The output file is opened in append mode**, so each start adds a new
  header line to an existing file. There is no built-in log rotation.
- **The header is printed before the first cycle**, so it also appears when
  none of the given PIDs exist.
- **Lenient number parsing:** `strtol` accepts leading whitespace and a `+`
  sign, so `-i " 5"` and `-i +5` are accepted as 5.

## Project structure

```
procmon/
├── src/
│   ├── main.c              # argument parsing, monitoring loop, output, signals, cleanup
│   ├── proc.c              # reading /proc: stat, status, uptime; finding PIDs by name
│   └── proc.h
├── tests/
│   ├── test_proc.c         # unit tests for the /proc functions
│   ├── run_scenarios.sh    # runs the 10 required test scenarios
│   └── results.txt         # recorded output of run_scenarios.sh
├── systemd/
│   ├── procmon.service     # systemd unit
│   └── procmon.conf        # service configuration (what to monitor, interval)
├── Makefile
└── README.md
```