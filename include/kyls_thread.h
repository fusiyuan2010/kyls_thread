#ifndef _KYLS_THREAD_H_
#define _KYLS_THREAD_H_
#include <stdlib.h>

typedef struct kyls_thread_t kyls_thread_t;

kyls_thread_t *kyls_thread_create(void (*sf_addr)(void *), void *sf_arg);
void kyls_thread_yield();
int kyls_thread_init();
void kyls_thread_sched();
void kyls_thread_destroy();
int kyls_thread_self();


void kyls_sleep(unsigned int seconds);
ssize_t kyls_read(int fd, void *buf, size_t n, int timeout_ms);
ssize_t kyls_write(int fd, void *buf, size_t n, int timeout_ms);

//int kyls_accept(int fd, struct sockaddr *restrict address, socklen_t *restrict address_len);


#endif



