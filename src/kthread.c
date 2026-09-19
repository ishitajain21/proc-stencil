#include "kthread.h"

kthread_t *curthr;

/*
 * TODO: implement me!
 * Hints: we don't have any threads running yet... but what from the thread
 * subsystem needs ot be initialized?
 */
void kthread_init() {
    slab_allocator_init(&kthread_allocator, sizeof(kthread_t));
    curthr = NULL;
}


/*
 * TODO: implement me!
 * Hints:
 *   - make space for the new thread using the kthread allocator
 *   - set default values for thread fields
 *   - you will need to allocate a kernel stack
 *   - you will need to set up the thread's context
 *     --> for now, the page table for the process is NULL
 *   - remember to add the thread to the proc's p_thread list
 *   - initialize the kt_recent_core to ~0UL (unsigned -1)
 *   - return NULL if allocation not possible
 */
kthread_t *kthread_create(proc_t *proc, kthread_func_t func, long arg1,
                          void *arg2) {
    kthread_t *thread = slab_obj_alloc(kthread_allocator);
    if (thread == NULL) {
        return NULL;
    }

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
    list_link_init(&thread->kt_plink, thread);
    list_link_init(&thread->kt_qlink, thread);

    spinlock_lock(&proc->p_threads_lock);
    list_insert_back(&proc->p_threads, &thread->kt_plink);
    spinlock_unlock(&proc->p_threads_lock);

    return thread;
}

/*
 * TODO: implement me!
 * Hints:
 *   - the only parts of the context that must be initialized are c_kstack and
 *     c_kstacksz
 *   - the thread's process should be set outside of this function
 *   - copy over the retval, errno, and cancelled... other fields should be
 *     freshly initialized
 *   - remember to protect access to the thread via its spinlock
 *   - see kthread_create for more hints!
 */
kthread_t *kthread_clone(kthread_t *old_thread) {
    kthread_t *thread = slab_obj_alloc(kthread_allocator);
    if (thread == NULL) {
        return NULL;
    }
    thread->kt_kstack = page_alloc_n(DEFAULT_STACK_SIZE_PAGES);
    if (thread->kt_kstack == NULL) {
        slab_obj_free(kthread_allocator, thread);
        return NULL;
    }

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
 * TODO: implement me!
 * Hints:
 *   - deallocate thread memory
 *   - remove thread from process' thread list
 *   - protect all accesses to shared data
 *   - don't forget to free thread's stack!
  cancel -> exit -> destroy 
 */
void kthread_destroy(kthread_t *thread) {
    spinlock_lock(&thread->kt_proc->p_threads_lock);
    // list_remove_link does not update head/tail
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
 * TODO: implement me!
 * Hints:
 *   - cannot "cancel" the current thread, so call exit
 *   - mark the thread as cancelled and stop executing
 *   - remember to the protect access to the thread
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
 * TODO: implement me!
 * Hints: there's (some but) not much to do here... remember, it's up to the
 * parent process to manage its threads!
 */
void kthread_exit(void *retval) {
    curthr->kt_retval = retval;
    curthr->kt_state = KT_EXITED;
    proc_thread_exiting(retval);
}
