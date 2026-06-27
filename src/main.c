#include "scheduler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Usage:
 *   ./scheduler <policy> <log_path> <name:priority:burst_ms> ...
 *
 * Policies: fcfs | sjf | srtf | rr | prio | mlq
 *
 * Example:
 *   ./scheduler rr logs/scheduler.log TaskA:5:200 TaskB:8:150 TaskC:3:300
 */

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <fcfs|sjf|srtf|rr|prio|mlq> <log_path> "
            "<name:priority:burst_ms> ...\n",
            argv[0]);
        return 1;
    }

    SchedPolicy policy = policy_from_string(argv[1]);

    log_init(argv[2]);
    log_event("[MAIN] Starting scheduler. policy=%s", argv[1]);

    scheduler_init(policy);

    for (int i = 3; i < argc; i++) {
        char name[32];
        int  prio, burst;

        if (sscanf(argv[i], "%31[^:]:%d:%d", name, &prio, &burst) != 3) {
            fprintf(stderr, "Bad process spec: %s  (expected name:prio:burst)\n",
                    argv[i]);
            continue;
        }

        PCB *p = process_create(name, prio, burst);
        process_submit(p);
    }

    scheduler_run();

    int count = 0;
    PCB **procs = scheduler_get_all_processes(&count);
    metrics_print(scheduler_get_metrics(), policy, procs, count);

    log_event("[MAIN] Scheduler exited cleanly");
    log_close();
    scheduler_destroy();
    return 0;
}
