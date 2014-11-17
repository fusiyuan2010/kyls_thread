#include <kyls_thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>


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
double time_diff_s(struct timeval *base)
{
    struct timeval tvnow;
    gettimeofday(&tvnow, NULL);
    double diff = (tvnow.tv_sec - base->tv_sec) * 1000000 + tvnow.tv_usec - base->tv_usec;
    return diff / 1000000;
}


static struct timeval tvbase;

void thread_proc(void *arg)
{
    int i;
    
    printf("Thread %ld began..\n", kyls_thread_self());
    for(i = 0; i < 7; i++) {
        int sec = rand() % 4;
        kyls_sleep(sec);
        printf("[%.3f] Thread %d printing %d, and slept %d seconds\n", 
                time_diff_s(&tvbase), kyls_thread_self(), i, sec);
    }
}

int main()
{
    int i;
    srand(time(NULL));
    gettimeofday(&tvbase, NULL);


    kyls_thread_init();
    for(i = 0; i < 5; i++) {
        kyls_thread_create(thread_proc, NULL);
    }
    kyls_thread_sched();
    printf("All thread termintated\n");
    kyls_thread_destroy();
    return 0;
}


