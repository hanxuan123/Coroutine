#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include "coroutine.h"
 
#define BUFFER_SIZE   1024
 
#define MAXEVENTS 64

int epfd;

struct socket_conn_t {
    int                 fd;
    int                 co;
    struct epoll_event *event;
    void               *data;
};

struct socket_conn_t *socket_conn_new(void)
{
    struct socket_conn_t *conn = malloc(sizeof(struct socket_conn_t));
    memset(conn, 0, sizeof(struct socket_conn_t));
 
    conn->event = (struct epoll_event *)malloc(sizeof(struct epoll_event));
    memset(conn->event, 0, sizeof(struct epoll_event));
 
    return conn;
}
void socket_conn_free(struct socket_conn_t *conn)
{
    if (conn == NULL) {
        return;
    }
 
    if (conn->fd != 0) {
        close(conn->fd);
        conn->fd = 0;
    }
 
    if (conn->event != NULL) {
        free(conn->event);
    }
 
    free(conn);
}

int socket_setnonblock(int fd)
{
    long flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        fprintf(stderr, "fcntl F_GETFL");
        return -1;
    }
 
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
        fprintf(stderr, "fcntl F_SETFL");
        return -1;
    }
 
    return 0;
}

int socket_create_and_bind(const char *port)
{
    int s, sfd;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
 
    s = getaddrinfo(NULL, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr,"%s getaddrinfo: %s\n", __func__, gai_strerror(s));
        return -1;
    }
 
    for (rp = result; rp!= NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
 
        s = socket_setopt(sfd);
        if (s == -1) {
            return -1;
        }
 
        s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
            break;
        }
        close(sfd);
    }
 
    if (rp == NULL) {
        fprintf(stderr, "%s Could not bind\n", __func__);
        return -1;
    }
 
    freeaddrinfo(result);
    return sfd;
}
 

int socket_setopt(int sockfd)
{
    int ret = 0;
 
    int reuse = 1;
    ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse));
    if (ret < 0) {
        fprintf(stderr, "%s setsockopt SO_REUSEPORT error\n", __func__);
        return -1;
    }
 
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 1;
    ret = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    if (ret < 0) {
        fprintf(stderr, "%s setsockopt SO_LINGER error\n", __func__);
        return -1;
    }
 
    return 0;
}

static void socket_service(struct schedule *schd, void *data)
{
    struct socket_conn_t *conn = (struct socket_conn_t *)data;
 
    char buffer[BUFFER_SIZE] = {0};
    while (1) {
        ssize_t read = recv(conn->fd, buffer, BUFFER_SIZE, 0);
        if (read < 0) {
            fprintf(stderr, "%s read error, fd:%d\n", __func__, conn->fd);
            return;
        }
 
        if (read == 0) {
            fprintf(stderr, "%s client disconnected, fd:%d\n", __func__, conn->fd);
            epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, conn->event);
            socket_conn_free(conn);
            return;
        }
 
        buffer[read] = '\0';
        fprintf(stderr, "%s receive from fd:%d message:%s", __func__, conn->fd, buffer);
 
        if (strncasecmp(buffer, "quit", 4) == 0) {
            fprintf(stderr, "%s client quit, fd:%d\n", __func__, conn->fd);
            epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, conn->event);
            socket_conn_free(conn);
            return;
        }
 
        conn->event->events = EPOLLOUT;
        epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, conn->event);
        coroutine_yield(schd);
 
        fprintf(stderr, "%s send to fd:%d message:%s", __func__, conn->fd, buffer);
 
        send(conn->fd, buffer, strlen(buffer), 0);
 
        conn->event->events = EPOLLIN;
        epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, conn->event);
        coroutine_yield(schd);
    }
}

void socket_accept(struct schedule *schd, void *data)
{
    int listen_fd = *(int *)data;
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);
 
    while (1) {
        int nfd = 0;
        if ((nfd = accept(listen_fd, (struct sockaddr *)&sin, &len)) == -1) {
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                fprintf(stderr, "%s bad accept\n", __func__);
            }
            coroutine_yield(schd);
            continue;
        }
 
        fprintf(stderr, "%s new conn %d [%s:%d]\n",
                __func__, nfd, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
 
        // new client
        struct socket_conn_t *conn = socket_conn_new();
        socket_setnonblock(nfd);
        conn->fd = nfd;
        conn->co = coroutine_new(schd, socket_service, conn);
        conn->event->data.fd = nfd;
        conn->event->data.ptr = conn;
        conn->event->events = EPOLLIN;
        epoll_ctl(epfd, EPOLL_CTL_ADD, nfd, conn->event);
    }
}


int main(int argc, char *argv[])
{
    int sfd = 0, s = 0;
    struct epoll_event event;
    struct epoll_event *events;
 
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return -1;
    }
 
    sfd = socket_create_and_bind(argv[1]);
    if (sfd == -1) {
        abort();
        return -1;
    }
 
    s = socket_setnonblock(sfd);
    if (s == -1) {
        abort();
        return -1;
    }
 
    s = listen(sfd, SOMAXCONN);
    if (s == -1) {
        fprintf(stderr, "listen error\n");
        abort();
        return -1;
    }
 
    epfd = epoll_create(256);
    if (epfd == -1) {
        fprintf(stderr, "epoll_create error\n");
        abort();
        return -1;
    }
 
    event.data.fd = sfd;
    event.events = EPOLLIN;
    s = epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &event);
    if (s == -1) {
        fprintf(stderr, "epoll_ctl error\n");
        abort();
        return -1;
    }
 
    struct schedule *schd = coroutine_open();
    int co_accept = coroutine_new(schd, socket_accept, &sfd);
 
    /* the event loop */
    events = calloc(MAXEVENTS, sizeof(event));
    while (1) {
        int i = 0, nfds = 0;
        nfds = epoll_wait(epfd, events, MAXEVENTS, 1000);
        for (i = 0; i < nfds; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                fprintf(stderr, "epoll_wait event error\n");
                close(events[i].data.fd);
                continue;
            }
            else if (events[i].events & EPOLLIN) {
                if (events[i].data.fd == sfd) {
                    /* listen socket, need accept */
                    if (coroutine_status(schd, co_accept)) {
                        coroutine_resume(schd, co_accept);
                    }
                }
                else {
                    /* client socket, need read */
                    struct socket_conn_t *conn = (struct socket_conn_t *)(events[i].data.ptr);
                    if (coroutine_status(schd, conn->co)) {
                        coroutine_resume(schd, conn->co);
                    }
                }
            }
            else if ((events[i].events & EPOLLOUT) && (events[i].data.fd != sfd)) {
                /* client socket, need write */
                struct socket_conn_t *conn = (struct socket_conn_t *)(events[i].data.ptr);
                if (coroutine_status(schd, conn->co)) {
                    coroutine_resume(schd, conn->co);
                }
            }
        }
    }
 
    free(events);
    close(epfd);
    close(sfd);
 
    return 0;
}