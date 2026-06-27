#ifndef LOGGER_H
#define LOGGER_H

#include "process.h"

void log_init(const char *path);
void log_close(void);

/* Log a state transition: PID name OLD_STATE -> NEW_STATE timestamp */
void log_transition(const PCB *p, ProcessState old_state, ProcessState new_state);

/* Log a generic event */
void log_event(const char *fmt, ...);

#endif /* LOGGER_H */
