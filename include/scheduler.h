#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"
#include "metrics.h"

#define MAX_PROCESSES     64
#define TIME_QUANTUM_MS   50
#define AGING_INTERVAL_MS 500
#define IO_INTERVAL_MS    100
#define IO_DURATION_MS    20

typedef struct {
    PCB          *queue[MAX_PROCESSES];
    int           count;
} ReadyQueue;

void scheduler_init(SchedPolicy policy);
void scheduler_destroy(void);

PCB *process_create(const char *name, int priority, int burst_ms);
void process_submit(PCB *p);
void process_wait(PCB *p);
void process_wake(PCB *p);
void process_terminate(PCB *p);

void scheduler_run(void);

const SchedMetrics *scheduler_get_metrics(void);
PCB **scheduler_get_all_processes(int *count);

void resource_init(SharedResource *r, int capacity, int init_val);
void resource_acquire(SharedResource *r, PCB *p);
void resource_release(SharedResource *r, PCB *p);
void resource_destroy(SharedResource *r);

void  mq_init(MessageQueue *mq);
void  mq_send(MessageQueue *mq, int sender_pid, const char *msg);
int   mq_recv(MessageQueue *mq, Message *out);
void  mq_destroy(MessageQueue *mq);

#endif /* SCHEDULER_H */
