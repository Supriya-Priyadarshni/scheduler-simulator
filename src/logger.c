#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE           *g_log  = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void timestamp(char *buf, size_t n) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm = localtime(&ts.tv_sec);
    char base[32];
    strftime(base, sizeof(base), "%H:%M:%S", tm);
    snprintf(buf, n, "%s.%03ld", base, ts.tv_nsec / 1000000L);
}

void log_init(const char *path) {
    g_log = fopen(path, "w");
    if (!g_log) { perror("log_init"); return; }
    /* CSV-style header so Python can parse with csv.DictReader */
    fprintf(g_log, "timestamp,pid,name,old_state,new_state,priority,remaining_ms\n");
    fflush(g_log);
}

void log_close(void) {
    if (g_log) { fclose(g_log); g_log = NULL; }
}

void log_transition(const PCB *p, ProcessState old_state, ProcessState new_state) {
    if (!g_log) return;
    char ts[32];
    timestamp(ts, sizeof(ts));

    pthread_mutex_lock(&g_lock);
    fprintf(g_log, "%s,%d,%s,%s,%s,%d,%d\n",
            ts,
            p->pid,
            p->name,
            state_name(old_state),
            state_name(new_state),
            p->priority,
            p->remaining_ms);
    fflush(g_log);
    pthread_mutex_unlock(&g_lock);
}

void log_event(const char *fmt, ...) {
    if (!g_log) return;
    char ts[32];
    timestamp(ts, sizeof(ts));

    pthread_mutex_lock(&g_lock);
    fprintf(g_log, "%s,–,–,EVENT,–,–,–  |  ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
    pthread_mutex_unlock(&g_lock);
}
