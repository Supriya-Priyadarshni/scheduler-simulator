#ifndef METRICS_H
#define METRICS_H

#include "process.h"

typedef struct {
    int context_switches;
    int cpu_busy_ms;
    int total_time_ms;
    int completed;
} SchedMetrics;

void metrics_init(SchedMetrics *m);
void metrics_record_dispatch(SchedMetrics *m, int had_running);
void metrics_record_cpu(SchedMetrics *m, int ms);
const char *policy_name(SchedPolicy policy);
void metrics_print(const SchedMetrics *m, SchedPolicy policy,
                   PCB *processes[], int count);

#endif /* METRICS_H */
