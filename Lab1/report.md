# Lab 1 Report: What does the middleman actually do?

Operating Systems, Session 1. Environment: Windows 11 (x64) with WSL2 and Ubuntu, machine name CSJ.

## 1. My definition of an operating system

**Before class:**
An operating system is software that manages the computer hardware and software resources and acts as a bridge between the user and the machine.

**Rewritten after Session 1:**
An operating system is software that manages computer hardware and software resources where the kernel is the heart acting as the direct bridge between applications and physical hardware, and the system call is a mechanism that allows a user-mode application to request a service from the kernel.

## 2. Lab A: Setup

I installed WSL2 and Ubuntu with `wsl --install -d Ubuntu`, then installed the tools with `sudo apt install -y build-essential gdb valgrind strace ltrace htop git man-db manpages-dev`.

| Check | Result |
|---|---|
| `gcc --version` | gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| `strace -V` | strace version 6.19 |
| `git --version` | git version 2.53.0 |

My lab folder is `Lab1`, inside the cloned `os-labs` repository.

## 3. Lab B: Interrogate the machine

| Command | What I saw | Answer |
|---|---|---|
| `uname -a` | `Linux CSJ 6.18.54.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC ... x86_64 GNU/Linux` | Kernel version 6.18.54.1 (the WSL2 kernel built by Microsoft). The architecture is x86_64, not aarch64. |
| `lscpu` | CPU(s): 4; Model name: AMD Ryzen 3 4300U with Radeon Graphics | Linux sees 4 CPUs. The model is AMD Ryzen 3 4300U. |
| `nproc` | 4 | Yes, it matches `lscpu` (4 CPUs). |
| `free -h` | total 3.5Gi, used 445Mi, free 3.0Gi, buff/cache 162Mi, available 3.1Gi | 3.1Gi is available. Available is bigger than free because it also counts memory the kernel can reclaim instantly, mainly file caches. Free is only memory nothing uses at all. |
| `df -h` | `/` is on `/dev/sdd` (ext4), size 1007G, used 1.9G, avail 954G | The filesystem mounted on `/` is `/dev/sdd` (ext4) with 954G left. That space is virtual, because Ubuntu lives in a disk file on Windows. The real limit is about 60G free on the Windows C: drive. |
| `ps aux \| head` | The first row is `root 1 ... /usr/lib/systemd/systemd --system` | PID 1 is `systemd`, the first process the kernel starts. |
| `ps aux \| wc -l` | 28 | 28 lines minus the header line gives 27 processes. That is about 7 times more than the 4 CPUs. |
| `htop` | At idle: all 4 CPUs at 0.0%, Tasks 26, 1 running | The machine was idle, so no process stood out. As a test I ran `yes > /dev/null &`. Then `yes` (PID 2960) used the most CPU, at 100% of one core, and I stopped it with `kill %1`. |

**Discussion: they cannot all run at the same instant, so how do they all make progress?**
In `htop` there were 26 tasks but only 1 was running at that moment (htop itself). The rest were sleeping, waiting for something such as a key press, a timer or network data. The kernel gives the 4 CPUs to whichever processes are ready and switches between them many times per second. Each process feels as if it has a CPU to itself. This is the "illusionist" job of the kernel.

## 4. Lab C: Every process is a folder

My shell's PID (`echo $$`) was **424**. `ls /proc/$$` showed its folder, full of files such as `status`, `exe`, `fd` and `maps`.

From `cat /proc/$$/status`:

| Field | Value | Meaning |
|---|---|---|
| `Name` | bash | The program is the Bash shell |
| `State` | S (sleeping) | Bash was waiting (for the command reading its file to finish) |
| `Pid` / `PPid` | 424 / 423 | Its own ID and its parent's ID (the WSL session process that started my terminal) |
| `Uid` | 1000 1000 1000 1000 | I run as user 1000, not root. The kernel checks this on every request. |
| `VmRSS` | 5756 kB | About 5.6 MB of physical RAM in use |
| `Threads` | 1 | A single thread |
| `voluntary_ctxt_switches` | 2966 (3025 on a later read) | Times bash gave up the CPU on purpose to wait |

- `readlink /proc/$$/exe` gave `/usr/bin/bash`.
- `ls -l /proc/$$/fd` showed descriptors `0`, `1` and `2` (and `255`) all pointing to `/dev/pts/0`, my terminal. Descriptor `1` is normal output, which Lab D uses again.

**The `sleep` process.** I ran `sleep 300 &` and got PID 3999.
- Its `State` was **S (sleeping)**, because `sleep` waits for a timer and uses no CPU meanwhile.
- Its `PPid` was **424**, my shell, because my shell started it.
- After `kill $!`, `ls /proc/3999` said "No such file or directory". The process died, so the kernel removed its folder.

**The whole machine lives in `/proc` too.** `head -20 /proc/cpuinfo` showed the same AMD Ryzen 3 4300U that `lscpu` reported. `head -5 /proc/meminfo` showed MemTotal 3,682,904 kB, MemFree 3,105,640 kB and MemAvailable 3,197,644 kB, which are the numbers `free -h` printed. `cat /proc/uptime` showed `26348.57 104531.02`: about 7.3 hours since boot, and idle time added up across all 4 CPUs.

**Why can I read `/proc/meminfo` when `ls -l` shows its size as 0?** The file stores no data on disk. When I read it, the kernel generates the contents on the spot from its live memory counters. The size shows 0 because nothing is stored. (The date shown is the current time, because the file is created as I read it.)

**Takeaway:** `ps`, `top` and `htop` are programs that read `/proc` and display it nicely.

## 5. Lab D: Watch the middleman work

`hello.c` (in this folder) prints `Hello, OS!` with one `printf`. I compiled it with `gcc -Wall -g hello.c -o hello`.

**Reading the trace.** In `strace ./hello` the important line is `write(1, "Hello, OS!\n", 11) = 11`. It means: the system call `write`, to file descriptor 1 (my terminal), the data "Hello, OS!\n", 11 bytes (10 characters plus the newline), and the return value 11 means all 11 bytes were written. Most lines come before `main` runs (`execve`, `brk`, `mmap`, `openat` and `read` for the C library), because loading the program is itself a conversation with the kernel. The trace ends with `exit_group(0)`.

### Experiments

**1. Number of system calls.** `strace -o strace-hello.txt ./hello` and `wc -l strace-hello.txt` gave **36 lines**. The saved file is `strace-hello.txt`.

**2. Most frequent call.** `strace -c ./hello` showed `mmap` as the most frequent call, with **8 calls**. Next were `mprotect`, `brk` and `fstat` with 3 each. Only 1 call was `write`. Most calls are about starting the program (mapping memory and loading libraries), not printing.

**3. Three printf calls.** I wrote `hello3.c` with three `printf` lines.
- `strace -e trace=write ./hello3` showed **3** `write` calls (7 bytes each).
- `strace -e trace=write ./hello3 > out.txt` showed **1** `write` call: `write(1, "Line 1\nLine 2\nLine 3\n", 21) = 21`.

My guess: when the output goes to the screen, the program writes after each line. When it goes to a file, it waits and writes everything at once, probably to save time, since each write is a trip into the kernel.

**4. Library calls vs system calls.** `ltrace ./hello` showed `puts("Hello, OS!") = 11`, not `printf`. The compiler swapped `printf` for the simpler `puts` because there are no format codes. `ltrace` shows calls to the C library, and `strace` shows calls to the kernel.

**5. Comparing with `ls`.** `strace -c ls` made about **144** system calls, against **34** for my program, so about **110 more**. `ls` also has to read the directory (`getdents64`), ask for each file's details (`statx`), check the terminal (`ioctl`) and read locale and configuration files. (The totals are the sums of the `calls` column.)

### What I saw when the referee said no

As a normal user, `cat /etc/shadow` printed `cat: /etc/shadow: Permission denied`. Tracing it with `strace -e trace=openat cat /etc/shadow` showed this line:

```
openat(AT_FDCWD, "/etc/shadow", O_RDONLY) = -1 EACCES (Permission denied)
```

`cat` asked the kernel to open the file of password hashes. The kernel compared my user ID (1000, from Lab C) with the file's permissions, which only allow root, and refused with `EACCES`. That one line is OS security in action: the kernel acts as the referee. A different line just above it, `-1 ENOENT (No such file or directory)`, was for a locale file that does not exist. `ENOENT` means the file is missing, while `EACCES` means it exists but I am not allowed in.

## 6. Going further (bonus)

1. **Why is `cat` stuck?** `strace cat` blocked in `splice(0, NULL, 4, NULL, 1048576, 0` (newer `cat` uses `splice` where older versions use `read`). Descriptor 0 is my keyboard, so it was waiting for input. When I typed `hello` and pressed Enter, the call returned 6 (five letters plus the newline) and `splice(3, NULL, 1, ..., 6, 0) = 6` sent them to the screen. Then it blocked again. Ctrl-D is end-of-file, so `splice` returned 0 and `cat` exited with `exit_group(0)`. Also, a `SIGWINCH` signal (window resize) interrupted the blocked call, and the kernel restarted it (`ERESTARTSYS`).

2. **My own `ps`.** `myps.sh` (in this folder) loops over every numbered folder in `/proc` and prints the PID, the name from `comm`, and the state letter from `status`. It showed `systemd` as PID 1, and every process in state `S`. It also listed its own processes (`bash`, `bash`, `head`).

3. **PID 1 and the referee.** `cat /proc/1/comm` gave `systemd`. As a normal user, `ls -l /proc/1/cwd` and `cat /proc/1/environ` both said "Permission denied". With `sudo` they worked: `cwd` is `/`, and the environment included `container=wsl`. The kernel allows only the process owner (root) and root itself to see a process's working folder and environment, because the environment can hold secrets. `sudo` made me root, so the kernel allowed it.

4. **Networking.** `strace -e trace=network curl -s http://example.com -o /dev/null` showed `socket` and `connect` calls. The IPv6 attempt failed with `ENETUNREACH`. The request was sent through the connection to **104.20.23.154, port 80** (`sendto(6, "GET / HTTP/1.1\r\nHost: example.co"..., 75) = 75`, where strace cuts the text short), and the reply was 936 bytes (`HTTP/1.1 200 OK`). Port 80 is the default for HTTP.

5. **Python's start-up cost.** `strace -o py.txt python3 -c 'print("Hello, OS!")'` produced **397** lines, against **36** for my C program, about 11 times more. Python loads its interpreter and standard library before running my one line, so a language runtime has a start-up cost.

6. **Preview of Session 2.** `strace -f -e trace=clone,clone3,execve bash -c "ls | wc -l"` showed **2** `clone` calls. One command line created 2 new processes (3 counting bash itself): one ran `execve` for `ls` and the other for `wc -l`. Each is made by `clone` (a fork) and then turned into a new program by `execve`.
