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

static void *childproc_run(long arg1, void *arg2) {
    (void)arg1;
    (void)arg2;
    return NULL;
}

static void *initproc_run(long arg1, void *arg2) {
    (void)arg1;
    (void)arg2;

   
    proc_t *child = proc_create("child");
    kthread_t *child_thr = kthread_create(child, childproc_run, 0, NULL);

    child->p_state = PROC_RUNNING;
    curthr->kt_state = KT_ON_CPU;
    list_insert_back(&kt_runq.tq_list, &child_thr->kt_qlink);
    sched_switch();

    // unlink the child from proc_list and our p_children before destroying it
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

    proc_destroy(child);
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

    void *bootstrap_stack = page_alloc_n(1);
    if (bootstrap_stack == NULL) {
        return -1;
    }

    context_setup(&bootstrap_ctx, start_initproc, 0, NULL, bootstrap_stack, PAGE_SIZE, NULL);
    context_switch(&bios_ctx, &bootstrap_ctx); // saves this as the place where bios ctx will restore

    // initproc exited cleanly after its child was created, run, and fully reaped.
    // nothing should remain in proc_list or idleproc's p_children.
    if (proc_initproc == NULL || proc_initproc->p_state != PROC_DEAD) {
        return 1;
    }
    if (proc_initproc->p_status != 0) {
        return 1;
    }
    if (proc_list.size != 0) {
        return 1;
    }
    if (idleproc.p_children.size != 0) {
        return 1;
    }
    if (next_pid != 3) {
        return 1;
    }
    if (curproc != proc_initproc) {
        return 1;
    }
    if (curthr == NULL || curthr->kt_state != KT_EXITED) {
        return 1;
    }

    return 0;
}
