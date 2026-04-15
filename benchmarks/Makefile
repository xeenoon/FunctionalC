CORE    = ../core/src
CC      = gcc
CFLAGS  = -Wall -Wextra -I$(CORE)
LDFLAGS = -lpthread
CORE_SRCS = $(CORE)/observable.c $(CORE)/list.c $(CORE)/task.c $(CORE)/stopwatch.c

SCENARIOS = map_filter_reduce

all: $(foreach s,$(SCENARIOS),$(s)_O0 $(s)_O2 $(s)_O3)

%_O0: %.c $(CORE_SRCS)
	$(CC) -O0 $(CFLAGS) $^ -o $@ $(LDFLAGS)

%_O2: %.c $(CORE_SRCS)
	$(CC) -O2 $(CFLAGS) $^ -o $@ $(LDFLAGS)

%_O3: %.c $(CORE_SRCS)
	$(CC) -O3 $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(foreach s,$(SCENARIOS),$(s)_O0 $(s)_O2 $(s)_O3)

.PHONY: all clean
