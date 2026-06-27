#include "process.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

const char *state_name(ProcessState s) {
    switch (s) {
        case STATE_NEW:        return "NEW";
        case STATE_READY:      return "READY";
        case STATE_RUNNING:    return "RUNNING";
        case STATE_WAITING:    return "WAITING";
        case STATE_TERMINATED: return "TERMINATED";
        default:               return "UNKNOWN";
    }
}

static const int valid[5][5] = {
/*            NEW  RDY  RUN  WAIT TERM */
/* NEW    */ { 0,   1,   0,   0,   0 },
/* READY  */ { 0,   0,   1,   0,   0 },
/* RUNNING*/ { 0,   1,   0,   1,   1 },
/* WAITING*/ { 0,   1,   0,   0,   0 },
/* TERM   */ { 0,   0,   0,   0,   0 },
};

int fsm_transition(PCB *p, ProcessState next) {
    ProcessState cur = p->state;

    if (!valid[cur][next]) {
        log_event("[FSM] INVALID transition PID %d %s -> %s",
                  p->pid, state_name(cur), state_name(next));
        return -1;
    }

    ProcessState old = p->state;
    p->state = next;
    log_transition(p, old, next);
    return 0;
}

SchedPolicy policy_from_string(const char *s) {
    if (!s) return SCHED_ROUND_ROBIN;
    if (strcmp(s, "fcfs") == 0) return SCHED_FCFS;
    if (strcmp(s, "sjf")  == 0) return SCHED_SJF;
    if (strcmp(s, "srtf") == 0) return SCHED_SRTF;
    if (strcmp(s, "rr")   == 0) return SCHED_ROUND_ROBIN;
    if (strcmp(s, "prio") == 0) return SCHED_PRIORITY;
    if (strcmp(s, "mlq")  == 0) return SCHED_MLQ;
    return SCHED_ROUND_ROBIN;
}
