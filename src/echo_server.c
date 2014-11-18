#include <kyls_thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>

void conn_proc(void *arg)
{
    char str[100];
    int fd = *(int *)arg;
    while(1)
    {
        int k = kyls_read(fd ,str,100, 0);
        if (k < 0)
            break;

        str[k] = '\0';
        kyls_sleep_ms(1000);
        printf("Echoing back - %s",str);
        kyls_write(fd, str, strlen(str)+1, 0);
    }
}

void server_proc(void *arg) 
{
    int listen_fd;
 
    struct sockaddr_in servaddr;
 
    listen_fd = kyls_socket(AF_INET, SOCK_STREAM, 0);
 
    bzero( &servaddr, sizeof(servaddr));
 
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(*(int*)arg);
 
    kyls_bind(listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
 
    kyls_listen(listen_fd, 10);
    
 
    while(1) {
        int comm_fd = kyls_accept(listen_fd, (struct sockaddr*) NULL, NULL, 0);
        if (comm_fd < 0)
            break;

        int *fd = (int *)malloc(sizeof(*fd));
        *fd = comm_fd;
        kyls_thread_create(conn_proc, (void *)fd);
    }
}

int main()
{
    int port = 8964;
    kyls_thread_init();
    kyls_thread_create(server_proc, &port);
    kyls_thread_sched();
    printf("All thread termintated\n");
    kyls_thread_destroy();
    return 0;
}


