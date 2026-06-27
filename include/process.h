#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <time.h>

/* ── Process State Machine ───────────────────────────────────────────────────
 *
 *   NEW ──► READY ──► RUNNING ──► TERMINATED
 *                        │              ▲
 *                        ▼              │
 *                     WAITING ─────────┘
 *
 * Transitions are logged to the run log on every state change.
 * ─────────────────────────────────────────────────────────────────────────── */

typedef enum {
    STATE_NEW        = 0,
    STATE_READY      = 1,
    STATE_RUNNING    = 2,
    STATE_WAITING    = 3,
    STATE_TERMINATED = 4
} ProcessState;

typedef enum {
    SCHED_FCFS        = 0,
    SCHED_SJF         = 1,
    SCHED_SRTF        = 2,
    SCHED_ROUND_ROBIN = 3,
    SCHED_PRIORITY    = 4,
    SCHED_MLQ         = 5
} SchedPolicy;

typedef struct {
    int            pid;
    char           name[32];
    ProcessState   state;
    int            priority;      /* 1 (low) – 10 (high) */
    int            burst_ms;      /* total CPU time needed (ms) */
    int            remaining_ms;  /* burst not yet consumed */
    int            wait_ms;       /* total time spent in READY */
    int            arrival_time;  /* simulated time at submission */
    int            start_time;    /* first RUNNING time (-1 if never) */
    int            finish_time;   /* TERMINATED time */
    int            response_ms;   /* start_time - arrival_time */
    int            turnaround_ms; /* finish_time - arrival_time */
    int            wake_time;     /* simulated time when I/O completes */
    int            consumed_ms;   /* CPU burst consumed so far */
    struct timespec enqueue_time; /* wall-clock for logging */
} PCB;                            /* Process Control Block */

/* Shared resource — protected by a real mutex + counting semaphore.
 * Even though scheduling is single-core simulated, this resource is
 * accessed by the main thread and can model concurrent contention
 * (e.g. logging thread, future extensions). */
#include <pthread.h>
#include <semaphore.h>

typedef struct {
    pthread_mutex_t lock;
    sem_t           slots;   /* counting semaphore: max concurrent holders */
    int             value;
    int             capacity;
} SharedResource;

/* One slot in the inter-task message queue */
typedef struct {
    int  sender_pid;
    char payload[128];
} Message;

#define MSG_QUEUE_SIZE 32

/* Message queue with real mutex + semaphore-based producer/consumer sync */
typedef struct {
    Message         buf[MSG_QUEUE_SIZE];
    int             head, tail, count;
    pthread_mutex_t lock;
    sem_t           items;   /* signals consumers: slots filled */
    sem_t           space;   /* signals producers: slots free   */
} MessageQueue;

const char *state_name(ProcessState s);
int         fsm_transition(PCB *p, ProcessState next);

SchedPolicy policy_from_string(const char *s);

#endif /* PROCESS_H */
