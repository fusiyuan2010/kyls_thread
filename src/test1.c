#include <kyls_thread.h>
#include <stdio.h>


/*
ssize_t kyls_read(int fd, void *buf, size_t n)
{
    //reg event
    //kyls_yield()
    //read();
    //
    return 0;
}

ssize_t kyls_write(int fd, void *buf, size_t n)
{
    //write();
    //kyls_yield
    // wait until all write done
    return 0;
}

int kyls_accept(int fd, struct sockaddr *restrict address, socklen_t *restrict address_len)
{
    //wait until event
    //return accept(fd, address, address_len);
    return 0;
}

*/

void thread_proc(void *arg)
{
    long long x = (long long)arg;
    int i;
    
    printf("Thread %lld began..\n", x);
    for(i = 0; i < 10; i++) {
        printf("Thread %lld printing %d..\n", x, i);
        kyls_thread_yield();
    }
}

int main()
{
    long long i;
    kyls_thread_init();
    for(i = 0; i < 5; i++) {
        kyls_thread_create(thread_proc, (void*)i);
    }
    kyls_thread_sched();
    return 0;
}



