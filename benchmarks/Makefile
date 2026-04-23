CORE    = ../core/src
CC      = gcc
CFLAGS  = -Wall -Wextra -I$(CORE) -DENABLE_PROFILER=0
LDFLAGS = -lpthread
CORE_SRCS = $(CORE)/observable.c $(CORE)/list.c $(CORE)/task.c $(CORE)/stopwatch.c $(CORE)/profiler.c

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
EXEEXT :=
endif

SCENARIOS = map_filter_reduce

all: $(foreach s,$(SCENARIOS),$(s)_O0$(EXEEXT) $(s)_O2$(EXEEXT) $(s)_O3$(EXEEXT))

%_O0$(EXEEXT): %.c $(CORE_SRCS)
	$(CC) -O0 $(CFLAGS) $^ -o $@ $(LDFLAGS)

%_O2$(EXEEXT): %.c $(CORE_SRCS)
	$(CC) -O2 $(CFLAGS) $^ -o $@ $(LDFLAGS)

%_O3$(EXEEXT): %.c $(CORE_SRCS)
	$(CC) -O3 $(CFLAGS) $^ -o $@ $(LDFLAGS)

ifeq ($(OS),Windows_NT)
%_O0: %_O0.exe

%_O2: %_O2.exe

%_O3: %_O3.exe
endif

clean:
	rm -f $(foreach s,$(SCENARIOS),$(s)_O0$(EXEEXT) $(s)_O2$(EXEEXT) $(s)_O3$(EXEEXT))

.PHONY: all clean
