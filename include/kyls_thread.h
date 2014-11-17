#ifndef _KYLS_THREAD_H_
#define _KYLS_THREAD_H_

typedef struct kyls_thread_t kyls_thread_t;

kyls_thread_t *kyls_thread_create(void (*sf_addr)(void *), void *sf_arg);
void kyls_thread_yield();
void kyls_thread_init();
void kyls_thread_sched();
void kyls_thread_destroy();

#endif



