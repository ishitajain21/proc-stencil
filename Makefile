CC = gcc
FLAGS = -Og -g -Wall
LIBFLAGS = -c
INCLUDE = -I. -Iinclude

# DEVS = mem disk cpu
DEVS = mem
UTILS = spinlock list string
ALLOCS = slaballocator
LIBS = proc kthread context sched
ALL = $(DEVS) $(UTILS) $(ALLOCS) $(LIBS)

# DEVOBJS = bin/mem.o bin/disk.o bin/cpu.o
DEVOBJS = bin/mem.o
UTILOBJS = bin/spinlock.o bin/list.o bin/string.o
ALLOCOBJS = bin/slab_allocator.o
LIBOBJS = bin/proc.o bin/kthread.o bin/context.o bin/sched.o
ALLOBJS = $(DEVOBJS) $(UTILOBJS) $(ALLOCOBJS) $(LIBOBJS)

mem:
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) sys/mem/mem.c -o bin/mem.o

spinlock:
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) util/spinlock.c -o bin/spinlock.o

list:
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) util/list.c -o bin/list.o

string:
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) util/string.c -o bin/string.o

slaballocator:
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) src/slab_allocator.c -o bin/slab_allocator.o

proc: $(UTILS) $(ALLOCS)
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE)  $(UTILOBJS) $(ALLOCOBJS) src/proc.c -o bin/proc.o

kthread: $(UTILS) $(ALLOCS)
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) $(UTILOBJS) $(ALLOCOBJS) src/kthread.c -o bin/kthread.o

context: $(UTILS) $(ALLOCS)
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) $(UTILOBJS) $(ALLOCOBJS) src/context.c -o bin/context.o

sched: $(UTILS) $(ALLOCS)
	$(CC) $(FLAGS) $(LIBFLAGS) $(INCLUDE) $(UTILOBJS) $(ALLOCOBJS) src/sched.c -o bin/sched.o

boottest: $(ALL)
	$(CC) $(FLAGS) $(INCLUDE) $(ALLOBJS) test/boot_test.c -o bin/boot_test

forktest: $(ALL)
	$(CC) $(FLAGS) $(INCLUDE) $(ALLOBJS) test/fork_test.c -o bin/fork_test

killtest: $(ALL)
	$(CC) $(FLAGS) $(INCLUDE) $(ALLOBJS) test/kill_test.c -o bin/kill_test

clean:
	rm -f bin/*
