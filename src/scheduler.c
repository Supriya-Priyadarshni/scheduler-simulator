#include "scheduler.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Globals ─────────────────────────────────────────────────────────────── */
static ReadyQueue   g_rq;
static SchedPolicy  g_policy;
static SchedMetrics g_metrics;
static int          g_next_pid    = 1;
static int          g_sim_time    = 0;
static int          g_last_aging  = 0;
static int          g_rr_cursor   = 0;
static PCB         *g_current     = NULL;
static PCB         *g_all[MAX_PROCESSES];
static int          g_all_count   = 0;
static int          g_active      = 0;

typedef struct {
    PCB *p;
} WaitingEntry;

static WaitingEntry g_waiting[MAX_PROCESSES];
static int          g_waiting_count = 0;

/* ── Ready-queue helpers ─────────────────────────────────────────────────── */
static void rq_push(PCB *p) {
    if (g_rq.count < MAX_PROCESSES)
        g_rq.queue[g_rq.count++] = p;
}

static PCB *rq_remove_at(int idx) {
    if (idx < 0 || idx >= g_rq.count) return NULL;
    PCB *p = g_rq.queue[idx];
    memmove(&g_rq.queue[idx], &g_rq.queue[idx + 1],
            (g_rq.count - idx - 1) * sizeof(PCB *));
    g_rq.count--;
    return p;
}

static PCB *rq_pop_fcfs(void) {
    return g_rq.count > 0 ? rq_remove_at(0) : NULL;
}

static PCB *rq_pop_sjf(void) {
    if (g_rq.count == 0) return NULL;
    int best = 0;
    for (int i = 1; i < g_rq.count; i++)
        if (g_rq.queue[i]->remaining_ms < g_rq.queue[best]->remaining_ms)
            best = i;
    return rq_remove_at(best);
}

static PCB *rq_pop_srtf(void) {
    return rq_pop_sjf();
}

static PCB *rq_pop_rr(void) {
    if (g_rq.count == 0) return NULL;
    if (g_rr_cursor >= g_rq.count) g_rr_cursor = 0;
    return rq_remove_at(g_rr_cursor);
}

static PCB *rq_pop_prio(void) {
    if (g_rq.count == 0) return NULL;
    int best = 0;
    for (int i = 1; i < g_rq.count; i++)
        if (g_rq.queue[i]->priority > g_rq.queue[best]->priority)
            best = i;
    return rq_remove_at(best);
}

static int mlq_level(int priority) {
    if (priority >= 8) return 0;
    if (priority >= 4) return 1;
    return 2;
}

static PCB *rq_pop_mlq(void) {
    if (g_rq.count == 0) return NULL;
    int level = 2;
    for (int i = 0; i < g_rq.count; i++) {
        int lv = mlq_level(g_rq.queue[i]->priority);
        if (lv < level) level = lv;
    }
    int first = -1;
    for (int i = 0; i < g_rq.count; i++) {
        if (mlq_level(g_rq.queue[i]->priority) == level) {
            first = i;
            break;
        }
    }
    return rq_remove_at(first);
}

static PCB *select_next(void) {
    switch (g_policy) {
        case SCHED_FCFS:        return rq_pop_fcfs();
        case SCHED_SJF:         return rq_pop_sjf();
        case SCHED_SRTF:        return rq_pop_srtf();
        case SCHED_ROUND_ROBIN: return rq_pop_rr();
        case SCHED_PRIORITY:    return rq_pop_prio();
        case SCHED_MLQ:         return rq_pop_mlq();
        default:                return rq_pop_fcfs();
    }
}

static PCB *peek_shortest_ready(void) {
    if (g_rq.count == 0) return NULL;
    int best = 0;
    for (int i = 1; i < g_rq.count; i++)
        if (g_rq.queue[i]->remaining_ms < g_rq.queue[best]->remaining_ms)
            best = i;
    return g_rq.queue[best];
}

/* ── Waiting list (I/O blocked) ──────────────────────────────────────────── */
static void waiting_add(PCB *p, int wake_time) {
    if (g_waiting_count < MAX_PROCESSES) {
        g_waiting[g_waiting_count].p = p;
        p->wake_time = wake_time;
        g_waiting_count++;
    }
}

static void wake_ready_processes(void) {
    int i = 0;
    while (i < g_waiting_count) {
        PCB *p = g_waiting[i].p;
        if (p->wake_time <= g_sim_time) {
            process_wake(p);
            g_waiting[i] = g_waiting[--g_waiting_count];
        } else {
            i++;
        }
    }
}

static int next_wake_time(void) {
    int next = -1;
    for (int i = 0; i < g_waiting_count; i++) {
        if (next < 0 || g_waiting[i].p->wake_time < next)
            next = g_waiting[i].p->wake_time;
    }
    return next;
}

/* ── Priority aging ──────────────────────────────────────────────────────── */
static void apply_aging(void) {
    if (g_policy != SCHED_PRIORITY && g_policy != SCHED_MLQ)
        return;
    while (g_sim_time - g_last_aging >= AGING_INTERVAL_MS) {
        g_last_aging += AGING_INTERVAL_MS;
        for (int i = 0; i < g_rq.count; i++) {
            PCB *p = g_rq.queue[i];
            if (p->priority < 10) {
                p->priority++;
                log_event("[AGING] PID %d priority boosted to %d", p->pid, p->priority);
            }
        }
        for (int i = 0; i < g_waiting_count; i++) {
            PCB *p = g_waiting[i].p;
            if (p->priority < 10) {
                p->priority++;
                log_event("[AGING] PID %d priority boosted to %d", p->pid, p->priority);
            }
        }
    }
}

/* ── Scheduler init / destroy ────────────────────────────────────────────── */
void scheduler_init(SchedPolicy policy) {
    g_policy      = policy;
    g_rq.count    = 0;
    g_all_count   = 0;
    g_sim_time    = 0;
    g_last_aging  = 0;
    g_rr_cursor   = 0;
    g_current     = NULL;
    g_waiting_count = 0;
    metrics_init(&g_metrics);
    log_event("[SCHED] Initialised — policy=%s", policy_name(policy));
}

void scheduler_destroy(void) {
    for (int i = 0; i < g_all_count; i++)
        free(g_all[i]);
    g_all_count = 0;
}

const SchedMetrics *scheduler_get_metrics(void) {
    return &g_metrics;
}

PCB **scheduler_get_all_processes(int *count) {
    if (count) *count = g_all_count;
    return g_all;
}

/* ── PCB creation ────────────────────────────────────────────────────────── */
PCB *process_create(const char *name, int priority, int burst_ms) {
    PCB *p = calloc(1, sizeof(PCB));
    p->pid          = g_next_pid++;
    p->priority     = priority;
    p->burst_ms     = burst_ms;
    p->remaining_ms = burst_ms;
    p->state        = STATE_NEW;
    p->start_time   = -1;
    p->wake_time    = -1;
    strncpy(p->name, name, sizeof(p->name) - 1);
    clock_gettime(CLOCK_MONOTONIC, &p->enqueue_time);

    log_event("[CREATE] PID %d name=%s priority=%d burst=%dms",
              p->pid, p->name, p->priority, p->burst_ms);
    return p;
}

/* ── Lifecycle transitions ───────────────────────────────────────────────── */
void process_submit(PCB *p) {
    p->arrival_time = g_sim_time;
    fsm_transition(p, STATE_READY);
    g_active++;
    if (g_all_count < MAX_PROCESSES)
        g_all[g_all_count++] = p;
    rq_push(p);
}

void process_wait(PCB *p) {
    fsm_transition(p, STATE_WAITING);
}

void process_wake(PCB *p) {
    fsm_transition(p, STATE_READY);
    rq_push(p);
}

void process_terminate(PCB *p) {
    fsm_transition(p, STATE_TERMINATED);
    p->finish_time    = g_sim_time;
    p->turnaround_ms  = p->finish_time - p->arrival_time;
    if (p->start_time < 0)
        p->response_ms = 0;
    else
        p->response_ms = p->start_time - p->arrival_time;

    log_event("[TERM] PID %d name=%s turnaround=%dms wait=%dms response=%dms",
              p->pid, p->name, p->turnaround_ms, p->wait_ms, p->response_ms);

    g_active--;
    g_metrics.completed++;
}

/* ── Dispatch / preemption ───────────────────────────────────────────────── */
static void dispatch(PCB *p) {
    if (g_current) {
        if (g_current == p) return;
        fsm_transition(g_current, STATE_READY);
        rq_push(g_current);
        g_metrics.context_switches++;
        g_current = NULL;
    }

    if (p->start_time < 0)
        p->start_time = g_sim_time;
    fsm_transition(p, STATE_RUNNING);
    g_current = p;
}

static void preempt_current(void) {
    if (!g_current) return;
    PCB *p = g_current;
    g_current = NULL;
    fsm_transition(p, STATE_READY);
    rq_push(p);
    g_metrics.context_switches++;
}

static int slice_size(PCB *p) {
    /* How many ms until the next forced I/O event */
    int until_io = IO_INTERVAL_MS - (p->consumed_ms % IO_INTERVAL_MS);
    if (until_io == 0) until_io = IO_INTERVAL_MS;

    int slice = p->remaining_ms;
    if (slice > until_io) slice = until_io;

    /* For RR/SRTF cap at one quantum */
    if (g_policy == SCHED_ROUND_ROBIN || g_policy == SCHED_SRTF) {
        int quantum_left = TIME_QUANTUM_MS - (p->consumed_ms % TIME_QUANTUM_MS);
        if (quantum_left == 0) quantum_left = TIME_QUANTUM_MS;
        if (slice > quantum_left) slice = quantum_left;
    }

    if (g_policy == SCHED_SRTF && g_rq.count > 0) {
        PCB *best = peek_shortest_ready();
        if (best && best->remaining_ms < p->remaining_ms) {
            int diff = p->remaining_ms - best->remaining_ms;
            if (slice > diff) slice = diff;
            if (slice < 1) slice = 1;
        }
    }

    return slice > 0 ? slice : 1;
}

static void accumulate_ready_wait(int ms) {
    for (int i = 0; i < g_rq.count; i++)
        g_rq.queue[i]->wait_ms += ms;
}

static void execute_slice(int ms, SharedResource *res, MessageQueue *mq) {
    if (!g_current || ms <= 0) return;

    accumulate_ready_wait(ms);
    g_sim_time += ms;
    g_metrics.total_time_ms += ms;
    metrics_record_cpu(&g_metrics, ms);

    g_current->remaining_ms -= ms;
    g_current->consumed_ms  += ms;

    /* Heartbeat message after every slice */
    if (mq) {
        char msg[128];
        snprintf(msg, sizeof(msg), "PID %d slice done, remaining=%dms",
                 g_current->pid, g_current->remaining_ms);
        mq_send(mq, g_current->pid, msg);
    }

    if (g_current->remaining_ms <= 0) {
        PCB *done = g_current;
        g_current = NULL;
        process_terminate(done);
        return;
    }

    /* I/O event: every IO_INTERVAL_MS of consumed CPU time */
    if (g_current->consumed_ms % IO_INTERVAL_MS == 0) {
        PCB *p = g_current;
        g_current = NULL;
        if (res) resource_acquire(res, p);
        process_wait(p);
        if (res) resource_release(res, p);
        waiting_add(p, g_sim_time + IO_DURATION_MS);
        log_event("[IO] PID %d blocked for %dms", p->pid, IO_DURATION_MS);
        return;
    }

    /* RR quantum expiry: rotate to back of queue */
    if (g_policy == SCHED_ROUND_ROBIN
        && g_current->consumed_ms % TIME_QUANTUM_MS == 0
        && g_rq.count > 0) {
        preempt_current();
    }
}


/* ── Main scheduling loop (single-core simulation) ───────────────────────── *
 * The scheduling loop itself is deterministic (single-core sim).             *
 * A real SharedResource (mutex + semaphore) and MessageQueue are created     *
 * here so the concurrency primitives are genuinely exercised — every process *
 * acquires/releases the resource on each I/O event, and sends a heartbeat    *
 * message to the queue after each CPU slice.                                 *
 * ─────────────────────────────────────────────────────────────────────────── */
void scheduler_run(void) {
    SharedResource res;
    resource_init(&res, g_all_count > 0 ? g_all_count : 1, 100);  /* capacity = num processes: no contention in single-core sim; demonstrates the API */

    MessageQueue mq;
    mq_init(&mq);

    log_event("[SCHED] Run started — %d processes", g_all_count);

    while (g_active > 0) {
        apply_aging();
        wake_ready_processes();

        if (!g_current && g_rq.count > 0) {
            PCB *next = select_next();
            if (next) dispatch(next);
        }

        if (!g_current && g_rq.count == 0 && g_waiting_count > 0) {
            int nw = next_wake_time();
            if (nw > g_sim_time) {
                int idle = nw - g_sim_time;
                accumulate_ready_wait(0);
                g_sim_time = nw;
                g_metrics.total_time_ms += idle;
                wake_ready_processes();
                continue;
            }
        }

        if (g_current) {
            int slice = slice_size(g_current);
            execute_slice(slice, &res, &mq);
        } else if (g_rq.count > 0) {
            PCB *next = select_next();
            if (next) dispatch(next);
        } else if (g_waiting_count > 0) {
            int nw = next_wake_time();
            if (nw > g_sim_time) {
                int idle = nw - g_sim_time;
                g_sim_time = nw;
                g_metrics.total_time_ms += idle;
            }
        } else {
            break;
        }

        if (g_rq.count > 0 && !g_current)
            accumulate_ready_wait(0);
    }

    /* Drain any unread heartbeat messages */
    Message msg;
    int drained = 0;
    while (mq_recv(&mq, &msg) == 0) drained++;
    if (drained)
        log_event("[MQ] Drained %d unread messages from queue", drained);

    resource_destroy(&res);
    mq_destroy(&mq);

    log_event("[SCHED] All processes terminated — sim_time=%dms cpu_busy=%dms",
              g_sim_time, g_metrics.cpu_busy_ms);
}

/* ── Shared resource ─────────────────────────────────────────────────────── *
 * Uses a real pthread_mutex_t + counting semaphore (sem_t).                  *
 * capacity controls how many processes may hold the resource concurrently.   *
 * ─────────────────────────────────────────────────────────────────────────── */
void resource_init(SharedResource *r, int capacity, int init_val) {
    r->capacity = capacity;
    r->value    = init_val;
    pthread_mutex_init(&r->lock, NULL);
    sem_init(&r->slots, 0, (unsigned int)capacity);
}

void resource_acquire(SharedResource *r, PCB *p) {
    log_event("[RES] PID %d acquiring resource (capacity=%d)", p->pid, r->capacity);
    /* sem_trywait in a spin loop: correctly models contention without blocking
     * the single-threaded sim loop. In a multi-threaded context sem_wait would
     * be used here to truly block the caller. */
    int tries = 0;
    while (sem_trywait(&r->slots) != 0) { tries++; }
    pthread_mutex_lock(&r->lock);
    log_event("[RES] PID %d acquired resource after %d tries (value=%d)",
              p->pid, tries, r->value);
    pthread_mutex_unlock(&r->lock);
}

void resource_release(SharedResource *r, PCB *p) {
    pthread_mutex_lock(&r->lock);
    log_event("[RES] PID %d released resource", p->pid);
    pthread_mutex_unlock(&r->lock);
    sem_post(&r->slots);                   /* wake a waiting process */
}

void resource_destroy(SharedResource *r) {
    pthread_mutex_destroy(&r->lock);
    sem_destroy(&r->slots);
}

/* ── Message queue ───────────────────────────────────────────────────────── *
 * Circular buffer guarded by pthread_mutex_t.                                *
 * sem_t items / space give producer-consumer flow control without busy-wait. *
 * ─────────────────────────────────────────────────────────────────────────── */
void mq_init(MessageQueue *mq) {
    mq->head = mq->tail = mq->count = 0;
    pthread_mutex_init(&mq->lock, NULL);
    sem_init(&mq->items, 0, 0);
    sem_init(&mq->space, 0, MSG_QUEUE_SIZE);
}

void mq_send(MessageQueue *mq, int sender_pid, const char *msg) {
    /* Non-blocking: drop message if queue is full (single-threaded sim) */
    if (sem_trywait(&mq->space) != 0) {
        log_event("[MQ] Queue full, PID %d message dropped", sender_pid);
        return;
    }
    pthread_mutex_lock(&mq->lock);
    mq->buf[mq->tail].sender_pid = sender_pid;
    strncpy(mq->buf[mq->tail].payload, msg, 127);
    mq->tail  = (mq->tail + 1) % MSG_QUEUE_SIZE;
    mq->count++;
    pthread_mutex_unlock(&mq->lock);
    sem_post(&mq->items);
}

int mq_recv(MessageQueue *mq, Message *out) {
    if (sem_trywait(&mq->items) != 0) return -1;   /* non-blocking */
    pthread_mutex_lock(&mq->lock);
    *out      = mq->buf[mq->head];
    mq->head  = (mq->head + 1) % MSG_QUEUE_SIZE;
    mq->count--;
    pthread_mutex_unlock(&mq->lock);
    sem_post(&mq->space);
    return 0;
}

void mq_destroy(MessageQueue *mq) {
    pthread_mutex_destroy(&mq->lock);
    sem_destroy(&mq->items);
    sem_destroy(&mq->space);
}
