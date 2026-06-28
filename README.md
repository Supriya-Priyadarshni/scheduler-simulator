# Mini Process Scheduler & State Machine Simulator

A systems-programming project in C that simulates an OS-style process scheduler with a Python automation and validation layer. Built to demonstrate state machine design, RTOS-style concurrency primitives, inter-task communication, and Python-driven test automation — core concepts in embedded firmware and OS development.

---

## What It Does

The scheduler accepts a list of processes (name, priority, burst time) and a scheduling policy via CLI, runs them through a simulated single-core CPU, and logs every state transition to a structured CSV file. A Python harness then spawns the binary, parses the log, and validates correctness automatically.

```
$ ./scheduler rr logs/run.log TaskA:5:200 TaskB:8:150 TaskC:3:300

===== Statistics =====
Policy          : Round Robin
CPU Utilization : 97.3%
Throughput      : 14.2 processes/sec
Context Switches: 12

PID  Name   Waiting  Turnaround  Response
1    TaskA  100ms    300ms       0ms
2    TaskB  140ms    290ms       50ms
3    TaskC   50ms    350ms       100ms
```

---

## Architecture

```
  CLI args (name:priority:burst_ms)
          │
          ▼
  ┌───────────────────────────────────┐
  │         scheduler (C binary)      │
  │                                   │
  │  process_create()                 │
  │       │                           │
  │       ▼                           │
  │  ┌─────────────────────────────┐  │
  │  │   FSM  (process.c)          │  │
  │  │                             │  │
  │  │  NEW → READY → RUNNING      │  │
  │  │               ↕        ↓    │  │
  │  │           WAITING   TERM    │  │
  │  └─────────────────────────────┘  │
  │       │                           │
  │       ▼                           │
  │  Scheduling loop (scheduler.c)    │
  │  · pthread_mutex_t  shared res    │
  │  · sem_t  counting semaphore      │
  │  · MessageQueue  producer/consumer│
  │       │                           │
  │       ▼                           │
  │  logger.c → logs/run.log (CSV)    │
  └───────────────────────────────────┘
          │
          ▼
  ┌───────────────────────────────────┐
  │    tests/test_scheduler.py        │
  │  · spawns binary per scenario     │
  │  · parses CSV log                 │
  │  · validates FSM transitions      │
  │  · checks all processes TERM      │
  │  · generates pass/fail report     │
  └───────────────────────────────────┘
```

---

## State Machine

```
  NEW ──► READY ──► RUNNING ──► TERMINATED
                       │              ▲
                       ▼              │
                    WAITING ──────────┘
```

Enforced in `src/process.c` via a 5×5 validity table — every transition is checked before it executes. Invalid transitions are logged and rejected; the scheduler never enters an inconsistent state. This is the same principle used in RTOS task state machines.

---

## Scheduling Algorithms

| Flag | Algorithm | Behaviour |
|---|---|---|
| `fcfs` | First Come First Served | Non-preemptive; processes run to completion in arrival order |
| `sjf` | Shortest Job First | Non-preemptive; shortest burst dispatched first |
| `srtf` | Shortest Remaining Time First | Preemptive SJF; preempts if a shorter job arrives |
| `rr` | Round Robin | Each process gets a fixed quantum (50ms); rotates on expiry |
| `prio` | Priority | Highest-priority process runs first; priority aging prevents starvation |
| `mlq` | Multilevel Queue | Separates processes into priority tiers with independent queues |

---

## Concurrency Primitives

| Primitive | Where | Purpose |
|---|---|---|
| `pthread_mutex_t` | `scheduler.c`, `logger.c` | Guards shared resource and log file against concurrent writes |
| `sem_t` (counting) | `SharedResource` | Bounds concurrent resource holders; `sem_trywait` used in single-core sim |
| `sem_t` (paired) | `MessageQueue` | Producer/consumer flow control — `items` signals consumers, `space` signals producers |

> In a multi-threaded deployment, `sem_trywait` would be replaced with `sem_wait` to truly block callers. The current design keeps the scheduling loop deterministic while still exercising the full POSIX API surface.

---

## Project Structure

```
scheduler_sim/
├── include/
│   ├── process.h       # PCB struct, ProcessState FSM, SharedResource, MessageQueue
│   ├── scheduler.h     # Scheduler API, ready-queue ops, lifecycle functions
│   ├── metrics.h       # Per-process and system-level metrics structs
│   └── logger.h        # CSV logger API
├── src/
│   ├── process.c       # FSM validity table and transition logic
│   ├── scheduler.c     # Scheduling loop, all 6 algorithms, mutex/sem/mq
│   ├── metrics.c       # Waiting time, turnaround, response time, CPU util, throughput
│   ├── logger.c        # Thread-safe CSV logger (mutex-protected)
│   └── main.c          # CLI entry point, arg parsing
├── tests/
│   └── test_scheduler.py   # Python automation harness — 4 scenarios, 4 invariants each
├── logs/                   # Generated at runtime
└── Makefile
```

---

## Build & Run

**Prerequisites:** `gcc`, `python3` (stdlib only — no pip installs)

```bash
# Build
make

# Round Robin
./scheduler rr logs/run.log TaskA:5:200 TaskB:8:150 TaskC:3:300 TaskD:7:100

# Priority scheduling
./scheduler prio logs/run.log LowPri:2:300 MidPri:5:200 HighPri:9:100

# SJF
./scheduler sjf logs/run.log Long:5:400 Short:5:80 Medium:5:150

# Run full automated test suite (4 scenarios, pass/fail report)
python3 tests/test_scheduler.py
```

**Process spec format:** `name:priority:burst_ms`
- `priority` — 1 (lowest) to 10 (highest)
- `burst_ms` — total CPU time the process needs

---

## Log Format

Every state transition is written as a CSV row:

```
timestamp,pid,name,old_state,new_state,priority,remaining_ms
09:14:22.101,1,TaskA,NEW,READY,5,200
09:14:22.101,1,TaskA,READY,RUNNING,5,200
09:14:22.101,1,TaskA,RUNNING,WAITING,5,150
09:14:22.101,1,TaskA,WAITING,READY,5,150
09:14:22.101,1,TaskA,READY,RUNNING,5,150
09:14:22.101,1,TaskA,RUNNING,TERMINATED,5,0
```

Designed for machine parsing — the Python harness reads this with `csv.DictReader` and reconstructs the full per-process state path.

---

## Python Test Harness

`tests/test_scheduler.py` runs 4 scenarios and checks 4 invariants per scenario:

| Scenario | Policy | Focus |
|---|---|---|
| Round Robin — Mixed Priorities | RR | 4 tasks, FSM + termination |
| Priority — Highest First | PRIO | High-priority task finishes first |
| Single Process | RR | Edge case — only one task |
| Many Short Tasks | PRIO | 6 tasks, all burst < 1 quantum |

**Invariants checked per scenario:**
1. **FSM Validity** — every transition is in the valid set; no illegal edges
2. **All Processes Terminate** — no process left in READY or WAITING
3. **No Return to NEW** — state machine never goes backwards to NEW
4. **Per-Process Sequence** — full path from NEW → TERMINATED is a valid FSM walk

---

## Key Concepts Demonstrated

- **FSM enforcement via validity table** — transitions checked against a 5×5 matrix, not ad-hoc if/else chains; same pattern used in RTOS task managers
- **RTOS-style scheduling** — round-robin with configurable time quanta, preemptive SRTF, priority with aging to prevent starvation
- **POSIX concurrency primitives** — `pthread_mutex_t` for critical sections, counting `sem_t` for resource bounding, paired semaphores for producer/consumer message queues
- **Structured logging for machine validation** — CSV output designed to be parsed, not just read; enables automated correctness checking
- **Python-driven test automation** — harness spawns binary, parses output, validates invariants, generates report; same workflow as firmware validation tooling