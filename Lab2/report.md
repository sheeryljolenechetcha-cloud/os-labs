# OS Session 2: How does a program come to life? (Lab report)

Environment: Windows 11, WSL2 Ubuntu (systemd enabled), 4 CPUs (`nproc` = 4).
All files are in this folder: `watch.sh`, `states.c`, `dstate.c`, `lifecycle.c`, `zombie.c`, `orphan.c`, `buffer.c`, plus `minish.c` (Going further) and `zombie-ps-l.png`.

---

## Lab A: Meet the family

| Command | Result |
|---|---|
| `echo $$ $PPID` | `364 362`: my shell is PID 364, its parent is 362 (`Relay(364)`, the WSL bridge process) |
| `pstree -p $$` | `bash(364)───pstree(11379)`: the shell forks `pstree` as its child |
| `pstree -p 1` | PID 1 is `systemd` (WSL with systemd enabled), not `init`. `init(7)` appears one level down |

State counts for all 30 processes (`ps -eo stat --no-headers | cut -c1 | sort | uniq -c`):

| Letter | Count |
|---|---|
| S | 26 |
| R | 3 |
| D | 1 |

- Most common letter: **S** (sleeping = Blocked), 26 of 30 (87%).
- No `I`: WSL2 does not show kernel threads.
- Running fraction: 3 of 30 = 10%. Three `R` were my own command pipeline (`ps`, `cut`, `sort`), so almost nothing else was running.
- **Conclusion:** processes spend nearly all of their lives Blocked, waiting for an event (key press, timer, network). They are rarely on a CPU. That is why a few CPUs can serve hundreds of processes.

---

## Lab B: Catch every state

I ran `./states` and watched it with `./watch.sh <pid>` (state letter from `/proc/<pid>/stat`; counters from `/proc/<pid>/status`).

| Phase | What the code does | Linux state | Textbook state | Counter that went up |
|---|---|---|---|---|
| 1 | `sleep(5)` | `S` | Blocked | voluntary |
| 2 | `burn(8)` | `R` | Running ⇄ Ready | involuntary (0 → 21) |
| 3 | `sleep(8)` | `S` | Blocked | voluntary (11 → 12) |
| 4 | `fgets` waiting for me | `S` | Blocked | voluntary (12 → 13) |
| 5 | `burn(5)` | `R` | Running ⇄ Ready | involuntary (24 → 36) |
| after | program ended | gone | Exit | none |

- `voluntary` rises when the process gives up the CPU itself to wait: **Running → Blocked**.
- `involuntary` rises when the scheduler takes the CPU away at the end of the time slice: **Running → Ready**.
- While Blocked (phases 1, 3, 4) `involuntary` stayed flat: a Blocked process is not on a CPU, so there is nothing to preempt.
- After exit I never saw `Z`: my shell is the parent and collected the exit status at once.

### Stopped (`T`)
- **Ctrl-Z in phase 2:** the state became `T` and both counters froze (`voluntary=9`, `involuntary=10`) for the ~40 s it stayed stopped. `fg` resumed it. Phase 2's `burn()` was skipped, because `burn()` measures wall-clock time and the deadline had already passed while it was stopped.
- **`kill -STOP` / `kill -CONT` from the shell:** same `T` state with frozen counters. After `kill -CONT` the state went back to `R` (Ready), and `involuntary` started growing again.
- **Difference:** Ctrl-Z sends `SIGTSTP`, which a program can catch or ignore. `kill -STOP` sends `SIGSTOP`, which cannot be caught, blocked or ignored. Both give `T`.
- **Textbook state:** Suspended (chosen by the user). The process is not waiting for an event, and the scheduler does not consider it.

### Uninterruptible sleep (`D`)
`./dstate &` followed by `ps -o pid,ppid,stat,wchan:20,comm`:

| PID | Role | STAT | WCHAN |
|---|---|---|---|
| 17985 | parent (`vfork` caller) | `D` | `kernel_clone` |
| 17990 | child (`sleep(20)`) | `S` | `hrtimer_nanosleep` |

- The parent is held inside `vfork` until the child calls `exec` or `_exit`.
- **`kill -9` on the `D` parent:** it died immediately (shell printed `Killed`). The child survived and was adopted by PID 11505.
- On this kernel the `vfork` wait is a *killable* sleep: `ps` shows `D`, but `SIGKILL` can end it. This differs from what the handout predicted. A true `D` wait (disk or dead network drive) ignores `kill -9` until the I/O returns.

### Idle (`I`)
`ps -eo pid,stat,comm | grep ' I'` printed nothing: WSL2 hides kernel threads. The kernel needs its own threads for background work that belongs to no user program: writing cached data to disk, reclaiming memory, handling deferred interrupt work and timers. They show `I` when idle.

---

## Lab C: Ready is not Running

- `nproc` = 4, so I started N + 2 = 6 `yes > /dev/null &` hogs.
- **`vmstat 1`:** before the hogs `r` = 0 and idle `id` ≈ 100%. After: `r` = 6 (steady), `id` ≈ 0–1%, `us` ≈ 38%, `sy` ≈ 62%, `b` = 0. Context switches `cs` rose from about 60 to about 450 per second.
- **Running vs Ready:** with 4 CPUs at most 4 processes are Running at any instant, so of the 6, about **4 were Running and 2 were Ready**, waiting for a CPU.
- `sy` is high because `yes` makes a `write` system call to `/dev/null` thousands of times a second.
- **Pinning:** `taskset -cp 0` on two hogs (PIDs 18109, 18110). `watch.sh 18109` showed state `R` all the time, `voluntary` stuck at 1, and `involuntary` growing about 70 per second (9811 → 11916 in 30 s).
- **Which arrow:** the `involuntary` counter counts **Running → Ready** (time slice used up, scheduler takes the CPU). The hog never waits for anything, so `voluntary` never moves.
- **Load average:** `/proc/loadavg` = `6.47 3.64 1.56`. A load above N = 4 means more runnable processes than CPUs, so on average at least 2 are always queued in Ready. The machine is overloaded. The 1-minute value is high and the 15-minute value low because the hogs had only run for a few minutes.
- Cleaned up with `pkill yes`.

**Debrief:** when a laptop "freezes" because one app goes crazy, the run queue (Ready) is full and every other process waits its turn.

---

## Lab D: Birth and death

### Birth: fork + exec + wait (`lifecycle.c`)
1. While the child runs `ls`, the parent is in `S` (Blocked): it sleeps inside `waitpid`, waiting for the event "child exited" (`bash` showed `S` with `do_wait` while a command ran).
2. With `"ls", "ls", "/nothing"` the parent printed `child exited with 2`. `ls /nothing; echo $?` also prints `2`. `waitpid` gives the parent the child's exit status.
3. `strace -f -e trace=clone,clone3,execve ./lifecycle`:
   - `clone(... SIGCHLD)` returned 18417: `clone` is the system call behind `fork`.
   - `execlp` searches every directory in `$PATH` in order. `/usr/local/sbin/ls`, `/usr/local/bin/ls` and `/usr/sbin/ls` failed with `ENOENT`, then `/usr/bin/ls` returned 0.
   - When the child exited, the parent received `SIGCHLD`.

### Death: zombie (`zombie.c`)
4. `./zombie & sleep 1; ps -lw; ps -ef | grep defunct` showed the child (PID 24583) with `S` column = **`Z`**, and its parent (PID 24581) sleeping (`S`, `hrtime`) and never calling `wait`. `ps -l` prints only the command name, so it does not show the word `<defunct>`; `ps -ef | grep defunct` does: `[zombie] <defunct>`. `<defunct>` means the process is dead but its process-table entry is still there, because the parent has not collected the exit status (`SZ` is 0: no memory is left). Five-state model: **Exit**.
5. `kill -9` on the zombie (PID 18474) changed nothing; `ps -l` still showed `Z`. It is already dead, so there is nothing left to kill. Only the parent's `wait()` (or the parent exiting, so that an adopter collects it) removes it.

*Why it matters:* a server that never waits fills the process table with zombies until `fork` fails ("Resource temporarily unavailable").

![ps -lw and ps -ef showing the zombie, state Z and [zombie] <defunct>](zombie-ps-l.png)

### Death: orphan (`orphan.c`)
6. The child first printed `my parent is 18641`. After the parent left it printed `my parent is 11505`, so the kernel re-parented it. `cat /proc/11505/comm` gives `Relay(11506)`: the WSL relay process, which acts as a subreaper (a process that adopts orphans and collects them later). PID 1 did not adopt it.

---

## Lab E: Suspended

1. `sleep 1000 &` was `S` and `yes > /dev/null &` was `R`.
2. After `kill -STOP` both were `T`. After `kill -CONT` they returned to what they were doing: `sleep` to `S`, `yes` to `R`. `SIGCONT` does not force a process to run; it lets it continue.
3. **Swap:** `free -h` shows Swap 1.0Gi, used 0B, and `VmSwap` is 0 kB for every process. Nothing was swapped out: there is plenty of free RAM (3.0Gi free of 3.5Gi).
4. **`T` versus Ready/Suspend:**

| | Linux `T` | Textbook Ready/Suspend |
|---|---|---|
| Who decides | The user or another process, by signal (Ctrl-Z, `kill -STOP`, a debugger) | The OS memory manager, when RAM is short |
| Why | Pause the process on purpose | Free up memory |
| Where is its memory | Still in RAM | Moved out to disk (swap) |
| How it comes back | `SIGCONT` | The OS brings it back into RAM, then it becomes Ready |

Both take a process out of the competition for the CPU. Linux does not swap out whole processes: it moves individual pages (`VmSwap`), so the process stays `R` or `S`. The suspend states survive in spirit as `T` and as phones freezing background apps.

5. Cleaned up with `pkill yes; pkill sleep`.

---

## Bug hunt: the `printf` that printed twice

`./buffer` printed `parent pid=...` **once** on the screen. `./buffer > out.txt; cat out.txt` showed the line **twice** in the file, with the same PID both times.

**Why:** `printf` does not call `write` straight away. The C library collects the text in a buffer in the process's own memory.
- When output goes to a **terminal**, the buffer is flushed at every newline, so the line leaves the process *before* `fork()`. Only the parent had it.
- When output goes to a **file**, the buffer is flushed only when it is full or when the program exits. So the text is still sitting in the buffer when `fork()` copies the whole memory. The child gets its own copy of the unflushed text, and both processes flush it when they exit. The line is written twice, and both copies carry the parent's PID because it was formatted before the fork.

**Fix:** `fflush(stdout);` before `fork()`.

This is also the answer to Session 1's question: with output to a terminal the three `printf` calls made three `write` calls (one per newline), but with `> out.txt` they were collected into one.

---

## Going further

### Build your own shell (`minish.c`)
- `ls`, `echo hello` worked and printed `[exit 0]`. `ls /nothing` printed the error and `[exit 2]` (the child's exit status, as in Lab D).
- First version: `cd /tmp` printed `cd: No such file or directory` and `[exit 127]`, and `pwd` still showed the Lab2 folder. `cd` is not a program (there is no `/usr/bin/cd`), so `execvp` failed in the child, and 127 is the shell convention for command not found. Even if `cd` were a program, it would change only the **child's** working directory, which is lost when the child exits. A working directory belongs to each process.
- Fix: make `cd` a **built-in** that the shell runs itself, in the parent, before `fork`, using `chdir()`. I also added `if (argc == 0) continue;` so a line of spaces does not crash it. After the fix, `cd /tmp` then `pwd` printed `/tmp`, and `cd /nothing` printed an error but the shell kept running.

### Debugger (`t` state)
- I rebuilt with `gcc -g` (without `-g` gdb said "No debugging symbols found"), ran `gdb ./states`, `break burn`, `run`. gdb stopped at `burn (seconds=8) at states.c:6`.
- From another terminal, `ps -eo pid,stat,comm | grep states` showed `18986 t states`: lowercase **`t`**, stopped under a debugger.
- Why a separate state: `T` is a stop requested by job control or a signal, and any `SIGCONT` can resume it. A debugged process must stay frozen so the debugger can read its registers and memory and step through it. Only the debugger should decide when it runs again.

### Security
- Run as `root`: `systemd`, `init`, `login`, `cron`, `systemd-journal`, `systemd-udevd`, `systemd-logind`, `agetty`, `unattended-upgr`, and the WSL `SessionLeader` and `Relay` processes.
- Run as service users: `chronyd` (`_chrony`), `rsyslogd` (`syslog`), `dbus-daemon` (`messagebus`), `polkitd`, `systemd-resolve`. Almost everything is `S` or `Ss` (sleeping).
- A web server should not run as root because it takes input from the whole internet. If an attacker finds a bug in it, they get that process's privileges. As root that means full control of the machine. Servers that need port 80 start as root, bind the port, then drop privileges.

### Networking
- `nc -l 5000` waiting for a connection: `S+` with `wchan = inet_csk_accept` (PID 19115), Blocked inside the kernel's TCP `accept` call. The event that wakes it is an incoming connection.
- After `nc localhost 5000` connected, both `nc` processes were `S+` in `poll_schedule_timeout`: Blocked in `poll()`, waiting for keyboard input or data from the socket.

### Data
- `time (for i in $(seq 1000); do /bin/true; done)`: `real` 2.307 s and 2.710 s in two runs, so about 369 to 433 processes per second (about 2.3 to 2.7 ms each). `sys` (1.4 to 1.7 s) is about twice `user`: most of the time is the kernel creating and destroying processes.
- A data job that starts one process per file pays this cost for every file. For 100,000 small files that is about 4 minutes of pure overhead. It is better to handle many files per process.
