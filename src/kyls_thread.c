#include <kyls_thread.h> 
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/time.h>

#define KYLS_SK_SIZE (1024 * 1024)

typedef struct {
    jmp_buf jb;
} mctx_t;

typedef struct kyls_thread_t {
    int tid;
    mctx_t ctx;
    struct timeval time_wakeup;

    void *sk_addr;
    size_t sk_size;
    struct kyls_thread_t *prev;
    struct kyls_thread_t *next;
} kyls_thread_t;

static kyls_thread_t *kyls_running_head, *kyls_running_tail;
static kyls_thread_t *kyls_sleeping_head;
static kyls_thread_t *kyls_current;
static kyls_thread_t *kyls_freelist;

static mctx_t sched_ctx;

static struct {
    int epollfd;
} kyls_ev;

static int global_tid_idx = 0;

static mctx_t *mctx_creat;
static mctx_t mctx_caller;
static sig_atomic_t mctx_called;
static void (*mctx_creat_func)(void *);
static void *mctx_creat_arg;
static sigset_t mctx_creat_sigs;

#define  mctx_save(mctx) \
    setjmp((mctx)->jb)


#define mctx_restore(mctx) \
    longjmp((mctx)->jb, 1)


#define mctx_switch(mctx_old, mctx_new) \
    if (setjmp((mctx_old)->jb) == 0) \
        longjmp((mctx_new)->jb, 1)


int time_diff_ms(struct timeval *base);
void mctx_create(mctx_t *mctx, void (*sf_addr)(void *), void *sf_arg, void *sk_addr, size_t sk_size);
void mctx_create_trampoline(int sig);
void mctx_create_boot(void);

void kyls_t_add_running(kyls_thread_t *t);
void kyls_t_remove_running(kyls_thread_t *t);
void kyls_t_yield_no_shed();
void kyls_t_switch_to(kyls_thread_t *t);
void kyls_t_free(kyls_thread_t *t);
void kyls_t_sched_no_sched();


int time_diff_ms(struct timeval *base)
{
    struct timeval tvnow;
    gettimeofday(&tvnow, NULL);
    long diff = (tvnow.tv_sec - base->tv_sec) * 1000000 + tvnow.tv_usec - base->tv_usec;
    return diff / 1000;
}

void mctx_create(mctx_t *mctx, void (*sf_addr)(void *), void *sf_arg, void *sk_addr, size_t sk_size)
{
    struct sigaction sa, osa;
    struct sigaltstack ss, oss;
    sigset_t sigs, osigs;

    /* step 1 */
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGUSR1);
    sigprocmask(SIG_BLOCK, &sigs, &osigs);

    /* step 2 */
    memset((void *)&sa, 0, sizeof(sa));
    sa.sa_handler = mctx_create_trampoline;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, &osa);

    /* step 3 */
    ss.ss_sp = sk_addr;
    ss.ss_size = sk_size;
    ss.ss_flags = 0;
    sigaltstack(&ss, &oss);

    /* step 4 */
    mctx_creat = mctx;
    mctx_creat_func = sf_addr;
    mctx_creat_arg = sf_arg;
    mctx_creat_sigs = osigs;
    mctx_called = false;
    kill(getpid(), SIGUSR1);
    sigfillset(&sigs);
    sigdelset(&sigs, SIGUSR1);
    while(!mctx_called)
        sigsuspend(&sigs);

    /* step 6 */
    sigaltstack(NULL, &ss);
    ss.ss_flags = SS_DISABLE;
    sigaltstack(&ss, NULL);
    if (!(oss.ss_flags & SS_DISABLE))
        sigaltstack(&oss, NULL);
    sigaction(SIGUSR1, &osa, NULL);
    sigprocmask(SIG_SETMASK, &osigs, NULL);

    /* step 7 & step 8 */
    mctx_switch(&mctx_caller, mctx);

    /* step 14 */
    return;
}


void mctx_create_trampoline(int sig)
{
    /* step 5 */
    if (mctx_save(mctx_creat) == 0) {
        mctx_called = true;
        return;
    }

    /* step 9*/
    mctx_create_boot();
}

void mctx_create_boot(void)
{
    void (*mctx_start_func)(void *);
    void *mctx_start_arg;

    /* step 10 */
    sigprocmask(SIG_SETMASK, &mctx_creat_sigs, NULL);

    /* step 11 */
    mctx_start_func = mctx_creat_func;
    mctx_start_arg = mctx_creat_arg;

    /* step 12 & step 13 */
    mctx_switch(mctx_creat, &mctx_caller);

    /* thread starts */
    mctx_start_func(mctx_start_arg);
/* impossible to reach */
    kyls_t_free(kyls_current);
    kyls_t_sched_no_sched();
}


void kyls_t_free(kyls_thread_t *t)
{
    // do not free now since it is still running on the stack
    // just put the thread context into free list
    t->next = kyls_freelist;
    t->prev = NULL;
    kyls_freelist = t;
}

void kyls_switch_to(kyls_thread_t *t)
{
    kyls_current = t;
    mctx_switch(&sched_ctx, &kyls_current->ctx);
}

void kyls_thread_yield()
{
    // add current task to the end of running queue
    kyls_t_add_running(kyls_current);

    // save context switch to scheduler
    mctx_switch(&kyls_current->ctx, &sched_ctx);
}

void kyls_t_sched_no_sched()
{
    // save context switch to scheduler
    mctx_switch(&kyls_current->ctx, &sched_ctx);
}

void kyls_thread_sched()
{
    static const int MAX_EVENTS = 10;
    kyls_current = NULL;
    while(kyls_running_head) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(kyls_ev.epollfd, events, MAX_EVENTS, 5);

        kyls_thread_t *cur;
        for(cur = kyls_running_head ; cur != NULL; ) {
            // pick up threads need to be scheduled that has no timer or timer expired
            if (cur->time_wakeup.tv_sec == 0 || time_diff_ms(&cur->time_wakeup) > 0) { 
                kyls_thread_t *t = cur;
                cur = cur->next;
                t->time_wakeup.tv_sec = 0;
                kyls_t_remove_running(t);
                kyls_switch_to(t);

                // may be not necessary
                kyls_current = NULL;
            } else {
                cur = cur->next;
            }
        }
    }
}

void kyls_t_add_running(kyls_thread_t *t)
{
    /* remove from sleeping list */
    /*
    if (kyls_sleeping_head == t) kyls_sleeping_head = t->next;
    if (t->next) t->next->prev = t->prev;
    if (t->prev) t->prev->next = t->next;
    */

    /* add to running list, the tail */
    t->next = NULL;
    t->prev = kyls_running_tail;

    if (!kyls_running_head) 
        kyls_running_head = t;

    if (kyls_running_tail)
        kyls_running_tail->next = t;

    kyls_running_tail = t;
}

void kyls_t_remove_running(kyls_thread_t *t)
{
    /* remove from running list */
    if (kyls_running_head == t) kyls_running_head = t->next;
    if (kyls_running_tail == t) kyls_running_tail = t->prev;
    if (t->next) t->next->prev = t->prev;
    if (t->prev) t->prev->next = t->next;
}

kyls_thread_t *kyls_thread_create(void (*sf_addr)(void *), void *sf_arg) 
{
    kyls_thread_t *t;

    // currently the stack size is fixed
    // fetch from free list if possible
    if (kyls_freelist) {
        t = kyls_freelist;
        t->next = t->prev = NULL;
        kyls_freelist = kyls_freelist->next;
    } else {
        // no freed, create a new one.
        t = (kyls_thread_t *)malloc(sizeof(*t));
        t->sk_size = KYLS_SK_SIZE;
        t->sk_addr = malloc(t->sk_size);
        t->next = t->prev = NULL;

        // tid will also be reused, like file descriptor
        t->tid = global_tid_idx++;
    }

    t->time_wakeup.tv_sec = 0;
    t->time_wakeup.tv_usec = 0;

    // create running context for this thread
    mctx_create(&t->ctx, sf_addr, sf_arg, t->sk_addr, t->sk_size);

    // make it will be scheduled
    kyls_t_add_running(t);
    return t;
}

void kyls_thread_destroy()
{
    // deallocate everyone in free list
    while(kyls_freelist) {
        kyls_thread_t *t = kyls_freelist->next;
        free(kyls_freelist->sk_addr);
        free(kyls_freelist);
        kyls_freelist = t;
    }

    kyls_running_head = kyls_running_tail = NULL;
    kyls_sleeping_head = NULL;
    kyls_current = NULL;
    kyls_freelist = NULL;
}

int kyls_thread_init()
{
    kyls_running_head = kyls_running_tail = NULL;
    kyls_sleeping_head = NULL;
    kyls_current = NULL;
    kyls_freelist = NULL;

    kyls_ev.epollfd = epoll_create(1);
    if (kyls_ev.epollfd < 0)
        return -1;
    return 0;
}

int kyls_thread_self()
{
    if (kyls_current)
        return kyls_current->tid;
    else
        return -1;
}

void kyls_sleep_ms(unsigned int milliseconds)
{
    if (!kyls_current)
        return;

    if (milliseconds) {
        gettimeofday(&kyls_current->time_wakeup, NULL);
        kyls_current->time_wakeup.tv_usec += (milliseconds % 1000) * 1000;
        kyls_current->time_wakeup.tv_sec +=
            (milliseconds / 1000) + kyls_current->time_wakeup.tv_usec / 1000000;
        kyls_current->time_wakeup.tv_usec %= 1000000;
    }

    kyls_thread_yield();
}

