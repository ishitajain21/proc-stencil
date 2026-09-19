#include "kthread.h"
#include "proc.h"
#include "sched.h"

/*
 * Additional processes-subsystem test: proc_kill.
 */

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
static const long KILL_STATUS = 99;

static void *childproc_run(long arg1, void *arg2) {
    return (void *)-1;
}

static void unlink_child(proc_t *child) {
    spinlock_lock(&curproc->p_children_lock);
    if (curproc->p_children.head == &child->p_child_link) {
        list_remove_front(&curproc->p_children);
    } else if (curproc->p_children.tail == &child->p_child_link) {
        list_remove_back(&curproc->p_children);
    } else {
        list_remove_link(&curproc->p_children, &child->p_child_link);
    }
    spinlock_unlock(&curproc->p_children_lock);

    spinlock_lock(&proc_list_lock);
    if (proc_list.head == &child->p_list_link) {
        list_remove_front(&proc_list);
    } else if (proc_list.tail == &child->p_list_link) {
        list_remove_back(&proc_list);
    } else {
        list_remove_link(&proc_list, &child->p_list_link);
    }
    spinlock_unlock(&proc_list_lock);
}

static void *initproc_run(long arg1, void *arg2) {
    proc_t *child = proc_create("to-kill");
    if (child == NULL) {
        return (void *)-1;
    }

    kthread_t *child_thr = kthread_create(child, childproc_run, 0, NULL);
    if (child_thr == NULL) {
        return (void *)-1;
    }

    if (child->p_threads.size != 1) {
        return (void *)-1;
    }
    if (proc_list.size != 2) { /* should just be: init + child */
        return (void *)-1;
    }

    proc_kill(child, KILL_STATUS);

    if (child->p_status != KILL_STATUS) {
        return (void *)-1;
    }
    if (child_thr->kt_cancelled != 1) {
        return (void *)-1;
    }
    if (child_thr->kt_retval != (void *)KILL_STATUS) {
        return (void *)-1;
    }
 
    unlink_child(child);
    proc_destroy(child);

    if (curproc->p_children.size != 0) {
        return (void *)-1;
    }
    if (proc_list.size != 1) { /* only init remains */
        return (void *)-1;
    }

    return NULL;
}

void *start_initproc(long arg1, void *arg2) {
    proc_initproc = proc_create("init");
    kthread_t *init_thread = kthread_create(proc_initproc, initproc_run, 0, NULL);

    curproc = proc_initproc;
    curthr = init_thread;

    context_make_active(&init_thread->kt_ctx);

    return NULL;
}

int main(int argc, char **argv) {
    for (int i = 0; i < NUM_INITS; i++) {
        init_funcs[i]();
    }

    void *bootstrap_stack = page_alloc_n(1);
    if (bootstrap_stack == NULL) {
        return -1;
    }

    context_setup(&bootstrap_ctx, start_initproc, 0, NULL, bootstrap_stack, PAGE_SIZE, NULL);
    context_switch(&bios_ctx, &bootstrap_ctx);

    if (proc_initproc == NULL || proc_initproc->p_state != PROC_DEAD) {
        return 1;
    }
    if (proc_list.size != 0) {
        return 1;
    }
    if (idleproc.p_children.size != 0) {
        return 1;
    }
    /* idle reserved 0, init=1, child=2 */
    if (next_pid != 3) {
        return 1;
    
    }

    return 0;
}
