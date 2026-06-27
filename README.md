# Mini Process Scheduler & State Machine Simulator

A systems-programming project that implements an OS-style process scheduler in C, with a Python automation and testing layer on top.  
Built to demonstrate: **state machine design**, **RTOS-style concurrency primitives**, **inter-task communication**, and **Python-driven test automation**.

---

## Architecture

```
                      ┌─────────────────────────────────┐
                      │         scheduler (C binary)     │
                      │                                  │
  CLI args ──────────►│  process_create()                │
  name:prio:burst     │      │                           │
                      │      ▼                           │
                      │  ┌──────────────────────────┐   │
                      │  │   FSM (process.c)         │   │
                      │  │                           │   │
                      │  │  NEW → READY → RUNNING    │   │
                      │  │              ↕       ↓    │   │
                      │  │           WAITING  TERM   │   │
                      │  └──────────────────────────┘   │
                      │      │                           │
                      │      ▼                           │
                      │  pthreads (one per process)      │
                      │  Shared resource: mutex+semaphore│
                      │  Message queue: send/recv        │
                      │      │                           │
                      │      ▼                           │
                      │  logger.c  ──► logs/run.log (CSV)│
                      └─────────────────────────────────┘
                                        │
                                        ▼
                      ┌─────────────────────────────────┐
                      │    tests/test_scheduler.py       │
                      │                                  │
                      │  • Spawns C binary per scenario  │
                      │  • Parses CSV log                │
                      │  • Validates FSM transitions     │
                      │  • Checks all processes TERM     │
                      │  • Prints pass/fail + stats      │
                      │  • Writes logs/test_report.txt   │
                      └─────────────────────────────────┘
```

---

## State Machine

```
  NEW ──► READY ──► RUNNING ──► TERMINATED
                       │              ▲
                       ▼              │
                    WAITING ──────────┘
```

The FSM is enforced in `src/process.c` via a validity table.  
Every illegal transition is logged and rejected — the scheduler never enters an inconsistent state.

---

## Concurrency Primitives Used

| Primitive | Where | Purpose |
|---|---|---|
| `pthread_t` | `scheduler.c` | One OS thread per simulated process |
| `pthread_mutex_t` | `scheduler.c`, `logger.c` | Protect shared resource and log file |
| `sem_t` (counting) | `SharedResource` | Limit concurrent resource holders (capacity=2) |
| `sem_t` (binary-style) | `MessageQueue` | Producer/consumer synchronisation |
| `pthread_cond_t` | `scheduler.c` | Main thread waits until all processes terminate |

---

## Scheduling Policies

| Policy | Flag | Behaviour |
|---|---|---|
| Round Robin | `rr` | Processes dispatched FIFO; each gets a `TIME_QUANTUM_MS` (50ms) slice |
| Priority | `prio` | Highest-priority process dispatched first; ties broken by submission order |

---

## Project Structure

```
scheduler_sim/
├── include/
│   ├── process.h       # PCB, ProcessState FSM, SharedResource, MessageQueue
│   ├── scheduler.h     # Scheduler API, ready-queue, lifecycle functions
│   └── logger.h        # CSV logger API
├── src/
│   ├── process.c       # FSM transition logic
│   ├── scheduler.c     # Round-robin + priority dispatch, pthreads, mutex/sem/mq
│   ├── logger.c        # Thread-safe CSV logger
│   └── main.c          # CLI entry point
├── tests/
│   └── test_scheduler.py  # Python automation harness (4 test scenarios)
├── logs/               # Generated at runtime
└── Makefile
```

---

## Build & Run

```bash
# Build
make

# Run manually (round-robin)
./scheduler rr logs/run.log TaskA:5:200 TaskB:8:150 TaskC:3:300

# Run manually (priority)
./scheduler prio logs/run.log LowPri:2:300 HighPri:9:100

# Run full automated test suite
python3 tests/test_scheduler.py
```

**Process spec format:** `name:priority:burst_ms`  
- `priority` — 1 (lowest) to 10 (highest)  
- `burst_ms` — total CPU time the process needs

---

## Log Format

The C binary writes a CSV log every run:

```
timestamp,pid,name,old_state,new_state,priority,remaining_ms
08:24:56.468,1,TaskA,NEW,READY,5,200
08:24:56.468,1,TaskA,READY,RUNNING,5,200
08:24:56.518,1,TaskA,RUNNING,READY,5,150
...
08:24:56.720,1,TaskA,RUNNING,TERMINATED,5,0
```

The Python harness reads this file, reconstructs per-process state sequences, and validates them against the FSM.

---

## Python Test Harness

`tests/test_scheduler.py` runs 4 scenarios automatically:

| Scenario | Policy | Validates |
|---|---|---|
| Round Robin — Mixed Priorities | RR | 4 tasks, FSM + termination |
| Priority — Highest First | PRIO | High-priority task finishes first |
| Single Process | RR | Edge case — only one task |
| Many Short Tasks | PRIO | 6 tasks, all burst < 1 quantum |

Each scenario checks:
1. **FSM Validity** — no illegal state transitions
2. **All Processes Terminate** — no process left hanging
3. **No Return to NEW** — state machine doesn't go backwards
4. **Per-Process State Sequence** — full path from NEW → TERMINATED is valid

---

## Key Concepts Demonstrated

- **State machine enforcement** with a validity table (not ad-hoc if/else)
- **RTOS-style task scheduling** — round-robin with time quanta, priority preemption
- **Concurrency without data races** — all shared state protected by mutex/semaphore
- **Inter-task message queues** — producer/consumer with semaphore-based flow control
- **Python-driven test automation** — spawns binary, parses structured logs, validates invariants, generates reports
- **Structured logging** — CSV output designed for machine parsing, not just human reading
