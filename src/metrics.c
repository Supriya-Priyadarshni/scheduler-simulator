#include "metrics.h"
#include "logger.h"
#include <stdio.h>

void metrics_init(SchedMetrics *m) {
    m->context_switches = 0;
    m->cpu_busy_ms      = 0;
    m->total_time_ms    = 0;
    m->completed        = 0;
}

void metrics_record_dispatch(SchedMetrics *m, int had_running) {
    if (had_running)
        m->context_switches++;
}

void metrics_record_cpu(SchedMetrics *m, int ms) {
    m->cpu_busy_ms += ms;
}

const char *policy_name(SchedPolicy policy) {
    switch (policy) {
        case SCHED_FCFS:        return "FCFS";
        case SCHED_SJF:         return "SJF";
        case SCHED_SRTF:        return "SRTF";
        case SCHED_ROUND_ROBIN: return "Round Robin";
        case SCHED_PRIORITY:    return "Priority";
        case SCHED_MLQ:         return "Multilevel Queue";
        default:                return "Unknown";
    }
}

void metrics_print(const SchedMetrics *m, SchedPolicy policy,
                   PCB *processes[], int count) {
    double util = (m->total_time_ms > 0)
        ? (100.0 * m->cpu_busy_ms / m->total_time_ms) : 0.0;
    double throughput = (m->total_time_ms > 0)
        ? (1000.0 * m->completed / m->total_time_ms) : 0.0;

    printf("\n===== Statistics =====\n\n");
    printf("Policy: %s\n\n", policy_name(policy));
    printf("CPU Utilization : %.1f%%\n", util);
    printf("Throughput      : %.1f processes/sec\n", throughput);
    printf("Context Switches: %d\n\n", m->context_switches);
    printf("PID  Waiting  Turnaround  Response\n");

    log_event("[STATS] policy=%s util=%.1f%% throughput=%.1f ctx=%d",
              policy_name(policy), util, throughput, m->context_switches);

    for (int i = 0; i < count; i++) {
        PCB *p = processes[i];
        printf("%-4d %-8d %-11d %dms\n",
               p->pid, p->wait_ms, p->turnaround_ms, p->response_ms);
        log_event("[STATS] PID %d wait=%d turnaround=%d response=%d",
                  p->pid, p->wait_ms, p->turnaround_ms, p->response_ms);
    }
    printf("\n");
}
