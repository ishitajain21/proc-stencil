#include "kthread.h"
#include "proc.h"
#include "sched.h"

const int NUM_INITS = 6;

typedef void (*init_func_t)();
init_func_t init_funcs[] = {
    mem_init,
    slab_init,
    proc_init,
    kthread_init,
    sched_init,
    proc_idleproc_init
};

static context_t bootstrap_ctx;

static void *initproc_run(long arg1, void *arg2) {
    return NULL;
}

void *start_initproc(long arg1, void *arg2) {
    proc_initproc = proc_create("init");
    kthread_t *init_thread = kthread_create(proc_initproc, initproc_run, 0, NULL);

    // don't worry about using the scheduling system...
    curproc = proc_initproc;
    curthr = init_thread;

    context_make_active(&init_thread->kt_ctx);

    return NULL;
}

int main(int argc, char **argv) {
    // initialize subsystems
    for (int i = 0; i < NUM_INITS; i++) {
        init_funcs[i]();
    }

    // After subsystem init, idleproc should be current with no threads.
    if (curproc != &idleproc) {
        return 1;
    }
    if (idleproc.p_pid != 0) {
        return 1;
    }
    if (idleproc.p_state != PROC_RUNNING) {
        return 1;
    }
    if (idleproc.p_pproc != NULL) {
        return 1;
    }
    if (idleproc.p_threads.size != 0) {
        return 1;
    }
    if (proc_list.size != 0) {
        return 1;
    }
    if (next_pid != 1) {
        return 1;
    }
    if (curthr != NULL) {
        return 1;

    }

    void *bootstrap_stack = page_alloc_n(1);
    if (bootstrap_stack == NULL) {
        return -1;
    }

    context_setup(&bootstrap_ctx, start_initproc, 0, NULL, bootstrap_stack, PAGE_SIZE, NULL);
    context_switch(&bios_ctx, &bootstrap_ctx); 
    if (proc_initproc == NULL) {
        return 1;
    }
    if (proc_initproc->p_pid != 1) {
        return 1;
    }
    if (proc_initproc->p_state != PROC_DEAD) {
        return 1;
    }
    if (proc_initproc->p_pproc != &idleproc) {
        return 1;
    }
    if (proc_initproc->p_status != 0) {
        return 1;
    }

    // Init was unlinked from the global list and from idle's children.
    if (proc_list.size != 0) {
        return 1;
    }
    if (idleproc.p_children.size != 0) {
        return 1;
    }
    if (idleproc.p_state != PROC_RUNNING) {
        return 1;
    }

    // We should still be "in" init's context as far as globals go.
    if (curproc != proc_initproc) {
        return 1;
    }
    if (curthr == NULL) {
        return 1;
    }
    if (curthr->kt_state != KT_EXITED) {
        return 1;
    }
    if (next_pid != 2) {
        return 1;
    }

    return 0;
}
