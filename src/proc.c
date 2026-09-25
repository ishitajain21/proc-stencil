#include "proc.h"

proc_t *curproc;

/*
 * @brief Initializes the process subsystem's global state before any processes exist
 */
void proc_init() {
    list_init(&proc_list); 
    spinlock_init(&proc_list_lock);
    next_pid = 0;
    proc_initproc = NULL;
    slab_allocator_init(&proc_allocator, sizeof(proc_t));
}

/*
 * @brief Sets up the special idleproc global, which is never placed in
 * proc_list and has no threads of its own. The idle process is the first
 * process on the system and is initialized at system startup.
 */
void proc_idleproc_init() {
    idleproc.p_pid = 0;
    strncpy(idleproc.p_name, "idleproc", MAX_STRING_LEN);
    idleproc.p_state = PROC_RUNNING;
    idleproc.p_status = 0;
    spinlock_init(&idleproc.p_threads_lock);
    spinlock_init(&idleproc.p_children_lock);
    list_init(&idleproc.p_threads);
    list_init(&idleproc.p_children);

    list_link_init(&idleproc.p_list_link, &idleproc);
    list_link_init(&idleproc.p_child_link, &idleproc);

    idleproc.p_pproc = NULL;
    next_pid = 1;
    curproc = &idleproc;
    curthr = NULL;         // no thread is running yet
}

/*
 * @brief This function is implemented to tell the system to shut down and exit
 */
void initproc_finish() {
    context_switch(&curthr->kt_ctx, &bios_ctx);
}

/*
 * @brief Allocates and initializes a new process, and links it into both
 * the global proc_list and the calling process's p_children list.
 *
 * @param name: the name to give the newly created process
 * @return the newly created process
 */
proc_t *proc_create(const char *name) {
    proc_t *new_proc = slab_obj_alloc(proc_allocator);
    if (new_proc == NULL) {
        return NULL;
    }

    spinlock_lock(&proc_list_lock);
    new_proc->p_pid = next_pid++;
    spinlock_unlock(&proc_list_lock);

    strncpy(new_proc->p_name, name, MAX_STRING_LEN);
    list_init(&new_proc->p_threads); 
    spinlock_init(&new_proc->p_threads_lock);
    list_init(&new_proc->p_children);
    spinlock_init(&new_proc->p_children_lock);
    new_proc->p_pproc = curproc;
    list_link_init(&new_proc->p_list_link, new_proc);
    list_link_init(&new_proc->p_child_link, new_proc);
    new_proc->p_status = 0;
    new_proc->p_state = PROC_PENDING;

    // register process with parent so it can later be found
    spinlock_lock(&curproc->p_children_lock);
    list_insert_back(&curproc->p_children, &new_proc->p_child_link);
    spinlock_unlock(&curproc->p_children_lock);

    // register process globally so the system can find/kill it
    spinlock_lock(&proc_list_lock);
    list_insert_back(&proc_list, &new_proc->p_list_link);
    spinlock_unlock(&proc_list_lock);  

    return new_proc;
}

/*
 * @brief Frees all resources owned by a process: its remaining kthreads and the
 * proc_t struct itself.
 *
 * @param proc: the process to destroy
 */
void proc_destroy(proc_t *proc) {
    spinlock_lock(&proc->p_threads_lock);
    while (proc->p_threads.head != NULL) {
        kthread_t *thr = (kthread_t *)proc->p_threads.head->parent;
        // kthread_destroy also takes p_threads_lock — unlock to avoid deadlock
        spinlock_unlock(&proc->p_threads_lock);
        kthread_destroy(thr);
        spinlock_lock(&proc->p_threads_lock);
    }
    spinlock_unlock(&proc->p_threads_lock);

    // return the proc_t memory to the allocator (nothing can reference
    // proc after this point)
    slab_obj_free(proc_allocator, proc);
}

/*
 * @brief Finalizes the currently-exiting process (curproc) once all of its
 * threads are done; marks it PROC_DEAD
 */
void proc_cleanup() {
    curproc->p_state = PROC_DEAD;

    // init shutting down: unlink, then switch back to bios
    if (curproc == proc_initproc) {
        if (curproc->p_pproc != NULL) {
            spinlock_lock(&curproc->p_pproc->p_children_lock);
            if (curproc->p_pproc->p_children.head == &curproc->p_child_link) {
                list_remove_front(&curproc->p_pproc->p_children);
            } else if (curproc->p_pproc->p_children.tail == &curproc->p_child_link) {
                list_remove_back(&curproc->p_pproc->p_children);
            } else {
                list_remove_link(&curproc->p_pproc->p_children, &curproc->p_child_link);
            }
            spinlock_unlock(&curproc->p_pproc->p_children_lock);
        }

        spinlock_lock(&proc_list_lock);
        if (proc_list.head == &curproc->p_list_link) {
            list_remove_front(&proc_list);
        } else if (proc_list.tail == &curproc->p_list_link) {
            list_remove_back(&proc_list);
        } else {
            list_remove_link(&proc_list, &curproc->p_list_link);
        }
        spinlock_unlock(&proc_list_lock);

        // init shutting down: switch back to bios before tearing down stacks
        initproc_finish(); // does not return
    }

    // only reached for non-init processes
    sched_switch();
}

/*
 * @brief Handles exiting of a thread running on the current process. Cancels every
 * other live thread in the process so the whole process winds down together
 *
 * @param retval: the exit code for the thread
 */
void proc_thread_exiting(void *retval) {
    curproc->p_status = (long)retval;

    spinlock_lock(&curproc->p_threads_lock);

    // hand retval to every OTHER thread so the process winds down
    list_link_t *link = curproc->p_threads.head;
    int others_alive = 0;
    while (link != NULL) {
        list_link_t *next = link->next;
        kthread_t *thr = (kthread_t *)link->parent;
        if (thr != curthr) {
            if (thr->kt_state != KT_EXITED) {
                others_alive++;
            }
            spinlock_unlock(&curproc->p_threads_lock);
            kthread_cancel(thr, retval);
            spinlock_lock(&curproc->p_threads_lock);
        }
        link = next;
    }
    spinlock_unlock(&curproc->p_threads_lock);

    // if this was the last live thread, finish the process
    if (others_alive == 0) {
        proc_cleanup();
    } else {
        sched_switch();   // let remaining threads run until they exit too
    }
}

 /*
 * @brief Stops another process from running again by cancelling
 * all of its associated threads and sets the exit status.
 *
 * @param proc: the process to kill
 * @param status: the status the process should exit with
 */
void proc_kill(proc_t *proc, long status) {
    proc->p_status = status;

    spinlock_lock(&proc->p_threads_lock);
    list_link_t *link = proc->p_threads.head;

    // loop over all threads and cancel them
    while (link != NULL) {
        list_link_t *next = link->next;
        kthread_t *thr = (kthread_t *)link->parent;
        spinlock_unlock(&proc->p_threads_lock);
        kthread_cancel(thr, (void *)status);
        spinlock_lock(&proc->p_threads_lock);
        link = next;
    }
    spinlock_unlock(&proc->p_threads_lock);
}

/*
 * @brief Kills every process except for idleproc and direct children of
 * idleproc, used for full-system shutdown.
 *
 * @param proc: the process to kill
 * @param status: the status the process should exit with
 */
void proc_kill_all() {
    spinlock_lock(&proc_list_lock);
    list_link_t *link = proc_list.head;
    while (link != NULL) {
        list_link_t *next = link->next;
        proc_t *proc = (proc_t *)link->parent;
        if (proc != &idleproc && proc->p_pproc != &idleproc && proc != curproc) {
            spinlock_unlock(&proc_list_lock);
            proc_kill(proc, 0);
            spinlock_lock(&proc_list_lock);
        }
        link = next;
    }
    spinlock_unlock(&proc_list_lock);

    // kill the current process
    proc_kill(curproc, 0);
}
