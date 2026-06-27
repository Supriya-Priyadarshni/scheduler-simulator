CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -I include -g
SRCS    = src/main.c src/scheduler.c src/process.c src/logger.c src/metrics.c
TARGET  = scheduler

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

.PHONY: all clean
