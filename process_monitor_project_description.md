# PROJECT — PROCESS MONITOR WITH CONTROL

## 1. EXPLANATION

The group will build a full-screen process monitor for the terminal, which, in addition to displaying the machine's state, allows acting upon it. All data must be obtained by directly querying the interface exposed by the operating system for its processes; the program cannot use an external tool to populate the table, nor any library that returns the list of processes or already calculated CPU percentages. The CPU percentage is calculated in code: the cumulative CPU time of each process and the total system time are taken in two consecutive refreshes, and the difference of the process is divided by the difference of the system.

The supported languages are C, C++, Go, and Rust. The program must be native to the operating system in which it runs.

---

## 2. WHAT IT MUST DO

The exact screen format is left to the group's discretion.

1. **Process table** with one row per process, containing the columns: identifier (PID), parent process (PPID), user, state, %CPU, %MEM, resident memory, number of threads, priority, CPU time, and command.
2. **System summary** at the top: total processes, how many are running, how many are stopped, how many are terminated without collection (zombies), load average, and total, used, and available memory.
3. **Own %CPU**, calculated by time difference between refreshes from the process and system counters; consistent with what the system reports.
4. **Sorting** by CPU and by memory: the process that consumes the most appears first.
5. **Process tree view**: the parent → child hierarchy.
6. **Thread view** of a process, with the state of each thread.
7. **CPU history** per process over time.
8. **Highlighting** of terminated uncollected processes and blocked processes waiting for I/O.
9. **Act on a process**: terminate, force terminate (with separate confirmation), suspend, and resume. After termination, the process disappears; after suspension, it is stopped; after resuming, it continues.
10. **Show and change** the priority of a process.
11. **Full-screen TUI**: occupies the entire terminal and uses its actual height and width (adapts if the window is resized); redraws in the same place on each refresh, without flickering or remnants of the previous table; distributes the header with the summary, table in the middle, and a help/input line at the bottom; and upon exiting, restores the terminal (normal echo, cursor, and prompt).
12. **Configurable refresh interval** from the interface.
13. **Hotkeys** for each action, responding instantly: exit, change interval, sort, filter by name, view tree, view threads, act on a process, and change priority.
14. **Action confirmation**: indicates which process and action were applied.
15. **Clear message**, without crashing, when a process no longer exists or there is no permission to query it.

---

## 3. IMPLEMENTATION RULES

### 3.1 Code Rules
* **Data comes from the operating system's interface.** The program directly queries the path exposed by the system to read the state of its processes (in Linux, the files in `/proc`; in macOS, `libproc` / `sysctl`; in Windows, Tool Help and PSAPI with their counters). That is the source of all data.
* **No pre-calculated data.** The CPU percentage, totals, and summary must be calculated in code. It is strictly forbidden to use libraries that return them resolved: `gopsutil` (Go), `sysinfo`, `procfs`, or `heim` (Rust), `libgtop` or `libprocps` (C/C++), or their equivalents in other systems.
* **No external programs.** The program must not execute `ps`, `top`, `tasklist`, `wmic`, PowerShell, `cmd`, or any other program, nor read their output, nor use them to send signals.