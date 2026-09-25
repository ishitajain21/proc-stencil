#include "kthread.h"

kthread_t *curthr;

/*
 * @brief Initializes the kthread subsystem at system startup.
 */
void kthread_init() {
    slab_allocator_init(&kthread_allocator, sizeof(kthread_t));
    curthr = NULL;   // no thread executing yet
}


/*
 * @brief Allocates a kernel thread with its own stack, sets up its context to run
 * func(arg1, arg2) once scheduled, and links it into its process's
 * p_threads list.
 *
 * @param proc: the process in which the thread will run
 * @param func: the function that will be called by the newly created thread
 * @param arg1: the first argument to func
 * @param arg2: the second argument to func
 * @return: the newly created thread
 */
kthread_t *kthread_create(proc_t *proc, kthread_func_t func, long arg1,
                          void *arg2) {
    kthread_t *thread = slab_obj_alloc(kthread_allocator);
    if (thread == NULL) {
        return NULL;
    }

    // each thread needs its own stack for context switching
    thread->kt_kstack = page_alloc_n(DEFAULT_STACK_SIZE_PAGES);
    if (thread->kt_kstack == NULL) {
        slab_obj_free(kthread_allocator, thread);
        return NULL;
    }

    thread->kt_retval = NULL;
    thread->kt_errno = 0;
    context_setup(&thread->kt_ctx, func, arg1, arg2, thread->kt_kstack,
                  DEFAULT_STACK_SIZE_PAGES * PAGE_SIZE, NULL);
    thread->kt_proc = proc;
    thread->kt_cancelled = 0;
    thread->kt_state = KT_RUNNABLE;
    spinlock_init(&thread->kt_lock);
    list_link_init(&thread->kt_plink, thread);  // links into p_threads
    list_link_init(&thread->kt_qlink, thread);  // links into scheduler's run queue

    spinlock_lock(&proc->p_threads_lock);
    list_insert_back(&proc->p_threads, &thread->kt_plink);
    spinlock_unlock(&proc->p_threads_lock);

    return thread;
}

/*
 * @brief Creates a copy of an existing thread with a fresh stack, to be used when
 * spawning a new process that should resume where an existing thread left off 
 *
 * @param thread: the thread to clone
 * @return: the newly created thread
 */
kthread_t *kthread_clone(kthread_t *old_thread) {
    kthread_t *thread = slab_obj_alloc(kthread_allocator);
    if (thread == NULL) {
        return NULL;
    }

    // clone gets its own stack
    thread->kt_kstack = page_alloc_n(DEFAULT_STACK_SIZE_PAGES);
    if (thread->kt_kstack == NULL) {
        slab_obj_free(kthread_allocator, thread);
        return NULL;
    }

    // copy over what should be carried forward from the original thread
    spinlock_lock(&old_thread->kt_lock);
    thread->kt_retval = old_thread->kt_retval;
    thread->kt_errno = old_thread->kt_errno;
    thread->kt_cancelled = old_thread->kt_cancelled;
    spinlock_unlock(&old_thread->kt_lock);

    thread->kt_ctx.c_kstack = thread->kt_kstack;
    thread->kt_ctx.c_kstacksz = DEFAULT_STACK_SIZE_PAGES * PAGE_SIZE;

    thread->kt_proc = NULL; // set by caller
    thread->kt_state = KT_RUNNABLE;
    spinlock_init(&thread->kt_lock);
    list_link_init(&thread->kt_plink, thread);
    list_link_init(&thread->kt_qlink, thread);

    return thread;
}

/*
 * Frees a thread's resources: its stack, its spot in the process's
 * thread list, and the kthread_t struct.
 *
 * @param thread: the thread to free
 */
void kthread_destroy(kthread_t *thread) {
    spinlock_lock(&thread->kt_proc->p_threads_lock);
    // list_remove_link does not update head/tail, so use front/back helpers
    // when the link is at an end
    if (thread->kt_proc->p_threads.head == &thread->kt_plink) {
        list_remove_front(&thread->kt_proc->p_threads);
    } else if (thread->kt_proc->p_threads.tail == &thread->kt_plink) {
        list_remove_back(&thread->kt_proc->p_threads);
    } else {
        list_remove_link(&thread->kt_proc->p_threads, &thread->kt_plink);
    }
    spinlock_unlock(&thread->kt_proc->p_threads_lock);
    page_free_n(thread->kt_kstack, DEFAULT_STACK_SIZE_PAGES);
    slab_obj_free(kthread_allocator, thread);
}

/*
 * Forcibly stops a thread. A thread cancelling itself is treated as an exit.
 *
 * @param thread: the thread to be canceled
 * @param retval: the reutrn value for the thread
 */
void kthread_cancel(kthread_t *thread, void *retval) {
    if (thread == curthr) {
        kthread_exit(retval);
        return;
    }
    spinlock_lock(&thread->kt_lock);
    thread->kt_cancelled = 1;
    thread->kt_retval = retval;
    spinlock_unlock(&thread->kt_lock);
}

/*
 * Exits the current thread, notifying its process
 *
 * @param retval: the return value for the thread
 */
void kthread_exit(void *retval) {
    curthr->kt_retval = retval;
    curthr->kt_state = KT_EXITED;
    proc_thread_exiting(retval);
}
