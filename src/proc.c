#include "proc.h"

proc_t *curproc;

/*
 * TODO: implement me!
 * Hints: we don't have any processes running yet... but what from the process
 * subsystem needs to be initialized?
 */

 /* 
 Suppose Bis a child of A. 
 List of realizations: 
 - A's p_children list should contain B's p_child_link
- B's p_pproc should point to A
proc_list should contain B's p_list_link

 */
void proc_init() {
    list_init(&proc_list); 
    spinlock_init(&proc_list_lock);
    next_pid = 0;
    proc_initproc = NULL;
    slab_allocator_init(&proc_allocator, sizeof(proc_t));
}

/*
 * TODO: implement me!
 * The idle process is a special process that is created by kmain
 * its job is to be the first process on the system, but it does not have any
 * associated threads
 * Hints:
 *   - what would the fields of the process struct be set to for idleproc?
 *   - what is the initial value of curproc? curthr? 
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
    curthr = NULL;
}

/*
 * This function is implemented to tell the system to shut down and exit
 */
void initproc_finish() {
    context_switch(&curthr->kt_ctx, &bios_ctx);
}

/*
 * TODO: implement me!
 * Hints:
 *   - make space for the new process using the process allocator
 *   - we need to update the global structures
 *   - the process becomes a child of the current process
 *   - don't forget to synchronize on shared structures!
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

    spinlock_lock(&curproc->p_children_lock);
    list_insert_back(&curproc->p_children, &new_proc->p_child_link);
    spinlock_unlock(&curproc->p_children_lock);

    spinlock_lock(&proc_list_lock);
    list_insert_back(&proc_list, &new_proc->p_list_link);
    spinlock_unlock(&proc_list_lock);  

    return new_proc;
}

/*
 * TODO: implement me!
 * Hints: anything that was allocated needs to be deallocated... deallocated
 * objects should not be accessible by anyone else!
 */
void proc_destroy(proc_t *proc) {
    // free stuff only — free what the process allocated
    // delete the threads associated with the process
    spinlock_lock(&proc->p_threads_lock);
    while (proc->p_threads.head != NULL) {
        kthread_t *thr = (kthread_t *)proc->p_threads.head->parent;
        // kthread_destroy also takes p_threads_lock — unlock to avoid deadlock
        spinlock_unlock(&proc->p_threads_lock);
        kthread_destroy(thr);
        spinlock_lock(&proc->p_threads_lock);
    }
    spinlock_unlock(&proc->p_threads_lock);

    // delete the process structure
    slab_obj_free(proc_allocator, proc);
}

/*
 * TODO: implement me!
 * Hints: anything that was allocated needs to be deallocated... deallocated
 * objects should not be accessible by anyone else!

 AKA finish the execution
 */
void proc_cleanup() {
    // process is not yet freed but done doing stuff, context switching, set
    // state, exiting, call destroy here
    // thread are done when they call proc thread exiting
    // proc thread exiting -> cleanup -> destroy
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

    sched_switch();
}

/*
 * TODO: implement me!
 * Hints: how should a process behave if all threads exit?
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
        sched_switch();
    }
}

/*
 * TODO: implement me!
 * Hints:
 *   - cancel all threads associated with the provided process
 *   - protect access to the threads list
 */
void proc_kill(proc_t *proc, long status) {
    // loop over all threads and cancel them
    // no need to do done
    proc->p_status = status;

    spinlock_lock(&proc->p_threads_lock);
    list_link_t *link = proc->p_threads.head;
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
 * TODO: implement me!
 * Hints:
 *  - protect access to the process list
 *  - kill the current process at the very end... don't kill before function
 * finishes!
 */
void proc_kill_all() {
    // for each process, kill each process
    // ensure that we don't kill the idle process or any direct children of the
    // idle process
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
