#define _DEFAULT_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include "network.h"
#include "config.h"

int set_nonblocking(int fd){
    int flags,result;
    flags = fcntl(fd,F_GETFL,0);
    if(flags == -1)
        goto err;
    result = fcntl(fd,F_SETFL,flags | O_NONBLOCK);
    if(result == -1)
        goto err;
    return 0;
err:
    perror("Set nonblocking");
    return -1;
}

// disable nagles algorithm by setting tcp_nodelay
int set_tcp_nodelay(int fd){
    //int setsockopt(int socket, int level, int option_name,const void *option_value, socklen_t option_len);
    // &(int){1} -> compound literal -> create a temporary int with value 1
    return setsockopt(fd, IPPROTO_TCP , TCP_NODELAY, &(int){1},sizeof(int));
}

static int create_and_bind_unix(const char* sockpath){
    struct sockaddr_un addr;
    int fd;
    //AF_UNIX(/AF_LOCAL) -> Address family UNIX -> used for unix domain sockets 
    // these are local sockets : they allow interprocess communication(ICP) on same machine
    // they use a file path instead of IP address and port
    if((fd = socket(AF_UNIX, SOCK_STREAM, 0))==-1){
        perror("socket error");
        return -1;
    }
    //struct sockaddr_un {
    // sa_family_t sun_family;     // Address family
    // char        sun_path[108];  // Path to the Unix socket file
    // };
    memset(&addr,0,sizeof(addr));
    addr.sun_family = AF_UNIX; //sets the address family for a Unix domain socket 

    strncpy(addr.sun_path,sockpath,sizeof(addr.sun_path)-1);
    unlink(sockpath);
    if(bind(fd,(struct sockaddr* )&addr,sizeof(addr))==-1){
        perror("bind error");
        return -1;
    }
    return fd;
}

static int create_and_bind_tcp(const char* host, const char* port){
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE
    };
    struct addrinfo *result,*rp;
    int sfd; // socket file descriptor
    if(getaddrinfo(host,port,&hints,&result)!=0){
        perror("getaddrinfo");
        return -1;
    }
    for(rp = result; rp!= NULL;rp=rp->ai_next){
        sfd = socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
        if(sfd == -1) continue;
        if(setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int))<0){
            perror("SO_REUSEADDR");
        }
        if((bind(sfd,rp->ai_addr,rp->ai_addrlen))==0){
            break;
        }
        close(sfd);
    }

    if(rp == NULL){
        perror("could not bind");
        return -1;
    }
    freeaddrinfo(result);
    return sfd;
}

int create_and_bind(const char* host,const char* port,int socket_family){
    int fd;
    if(socket_family == UNIX){
        fd = create_and_bind_unix(host);
    }else{
        fd = create_and_bind_tcp(host,port);
    }
    return fd;
}

int make_listen(const char* host,const char* port,int socket_family){
    int sfd;
    if((sfd = create_and_bind(host,port,socket_family))==-1){
        abort(); // defined in stdlib.h
    }
    if((set_nonblocking(sfd))==-1)
        abort();
    // set tcp_nodelay for tcp socket
    if(socket_family == INET){
        set_tcp_nodelay(sfd);
    }
    if((listen(sfd,conf->tcp_backlog))==-1){
        perror("listen");
        abort();
    }
    return sfd;
}

int accept_connection(int serversock){
    int clientsock;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if((clientsock = accept(serversock,(struct sockaddr *)&addr,&addrlen))<0)
        return -1;
    set_nonblocking(clientsock);

    if(conf->socket_family == INET){
        set_tcp_nodelay(clientsock);
    }

    char ip_buff[INET_ADDRSTRLEN + 1];
    if(inet_ntop(AF_INET, &addr.sin_addr,ip_buff,sizeof(ip_buff))==NULL){ // network to presentation
        close(clientsock);
        return -1;
    }
    return clientsock;
}

ssize_t send_bytes(int fd,const unsigned char* buf,size_t len){
    size_t total = 0;
    size_t bytesleft = len;
    ssize_t n = 0;
    while(total<len){
        n = send(fd, buf+total,bytesleft,MSG_NOSIGNAL); 
        // MSG_NOSIGNAL ->  prevent your program from being killed when sending data on a broken connection
        if(n == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                goto err;
        }
        total+=n;
        bytesleft -= n;
    }
    return total;
err:
    //The send system call — found in section 2, which is for system calls.
    fprintf(stderr, "send(2) - error sending data: %s", strerror(errno));//  the number in parentheses indicates the section of the manual
    return -1;    
}

// Reference 
// | Section | Contents                                              |
// | ------- | ----------------------------------------------------- |
// | 1       | User commands (`ls`, `cp`, etc.)                      |
// | 2       | **System calls** (like `read()`, `write()`, `send()`) |
// | 3       | Library functions (`printf()`, `malloc()` etc.)       |
// | 4       | Device files                                          |
// | 5       | File formats and configs                              |
// | 6       | Games                                                 |
// | 7       | Misc (protocols, conventions)                         |
// | 8       | System administration tools                           |


ssize_t recv_bytes(int fd, unsigned char* buf,size_t bufsize){
    ssize_t n = 0;
    ssize_t total = 0;
    while(total < (ssize_t)bufsize){
        if((n=recv(fd,buf,bufsize-total,0))<0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }else{
                goto err;
            }
        }
        if(n==0) return 0;
        buf+=n;
        total+=n;
    }
    return total;
err:
    fprintf(stderr,"recv(2) - error reading data: %s",strerror(errno));
    return -1;    
}


// typedef union epoll_data{ 
//     void* ptr;
//     int fd;
//     uint32_t u32;
//     uint64_t u64;
// }epoll_data_t;


#define EVLOOP_INITIAL_SIZE 4

struct evloop* evloop_create(int max_events,int timeout){
    struct evloop* loop = malloc(sizeof(*loop));
    evloop_init(loop,max_events,timeout);
    return loop;
}

//struct epoll_event {
    // uint32_t     events;    // Epoll events (e.g., EPOLLIN, EPOLLOUT)
    // epoll_data_t data;      // User data (usually you set .fd or .ptr)
// };

void evloop_init(struct evloop* loop,int max_events,int timeout){
    loop->max_events = max_events;
    loop->events = malloc(sizeof(struct epoll_event)*max_events);
    loop->epollfd = epoll_create1(0);
    loop->timeout = timeout;
    loop->periodic_maxsize = EVLOOP_INITIAL_SIZE;
    loop->periodic_nr = 0;
    loop->periodic_tasks = malloc(EVLOOP_INITIAL_SIZE * sizeof(*loop->periodic_tasks));
    loop->status = 0;
}


void evloop_free(struct evloop* loop){
    free(loop->events);
    for(int i = 0;i<loop->periodic_nr;i++)
        free(loop->periodic_tasks[i]);
    free(loop->periodic_tasks);
    free(loop);
}


int epoll_add(int efd,int fd,int evs,void* data){
    struct epoll_event ev;
    ev.data.fd = fd;
    if(data)
        ev.data.ptr = data;
    //EPOLLET — Edge-Triggered Mode ->Only notify me when new data arrives, not as long as data is available.
    //EPOLLONESHOT — One-shot Notification -> Notify me only once. I’ll manually rearm it.
    //useful for multi threaded servers -> to ensure only one thread handles a socket at a time
    ev.events = evs | EPOLLET | EPOLLONESHOT;
    //  combo of EPOLLET | EPOLLONESHOT 
    //  Edge-triggered for fewer notifications
    //  One-shot so only one thread handles the event, and you rearm when you're ready


    return epoll_ctl(efd,EPOLL_CTL_ADD,fd,&ev);
}

int epoll_mod(int efd,int fd,int evs,void* data){
    struct epoll_event ev;
    ev.data.fd =fd;
    if(data)
        ev.data.ptr = data;
    ev.events = evs | EPOLLET | EPOLLONESHOT;
    return epoll_ctl(efd,EPOLL_CTL_MOD,fd,&ev);
}

int epoll_del(int efd,int fd){
    return epoll_ctl(efd,EPOLL_CTL_DEL,fd,NULL);
}

void evloop_add_callback(struct evloop* loop,struct closure* cb){
    if(epoll_add(loop->epollfd,cb->fd,EPOLLIN,cb)<0)
        perror("Epoll register callback");
}

//adds a periodic task (i.e., a function to run every seconds + ns) to a custom event loop
void evloop_add_periodic_task(struct evloop* loop,int seconds,unsigned long long ns, struct closure* cb){
    // struct timerspec {
    //     time_t tv_sec;
    //     long tv_nsec;
    // }
    // struct itimerspec {
    //      struct timerspec it_interval;  time interval
    //      struct timerspec it_value;     initial expiration
    // }
    struct itimerspec timervalue; //itimerspec used with POSIX timers -> time.h -> Interval Timer Specification
    
    //int timerfd_create(int clockid, int flags);
    // It returns a file descriptor that becomes readable whenever the timer expires.
    int timerfd = timerfd_create(CLOCK_MONOTONIC,0); // creates a timer file descriptor using timerfd_create() system call
    //a Linux-specific feature that lets you use timers as file descriptors (which can be monitored with epoll, poll, or select)

    memset(&timervalue,0x00,sizeof(timervalue));

    // set initial expire time and periodic interval

    timervalue.it_value.tv_sec = seconds;
    timervalue.it_value.tv_nsec = ns;
    timervalue.it_interval.tv_sec = seconds;
    timervalue.it_interval.tv_nsec = ns;
    
    // This sets both:
    //     it_value: when the timer first fires
    //     it_interval: how often to repeat afterward
    //     If it_interval is 0, the timer would be one-shot (not repeating).

    if(timerfd_settime(timerfd,0,&timervalue,NULL)<0){
        perror("timerfd_settime");
        return;
    }

    struct epoll_event ev;
    ev.data.fd = timerfd;
    ev.events = EPOLLIN;

    if(epoll_ctl(loop->epollfd,EPOLL_CTL_ADD,timerfd,&ev)<0){  //Whenever the timer expires, epoll_wait() will return this fd as ready for reading.
        perror("epoll_ctl(2): EPOLLIN");
        return;
    }

    //This checks if the internal storage array (periodic_tasks) is large enough to hold another task.
    //If not, it doubles the size using realloc
    if(loop->periodic_nr + 1 > loop->periodic_maxsize){
        loop->periodic_maxsize *= 2;
        loop->periodic_tasks = realloc(loop->periodic_tasks,loop->periodic_maxsize * sizeof(*loop->periodic_tasks));
    }
    
    loop->periodic_tasks[loop->periodic_nr] = malloc(sizeof(*loop->periodic_tasks[loop->periodic_nr]));
    loop->periodic_tasks[loop->periodic_nr]->closure = cb;
    loop->periodic_tasks[loop->periodic_nr]->timerfd = timerfd;
    loop->periodic_nr++;
}


int evloop_wait(struct evloop* el){
    int rc = 0;
    int events = 0;
    long int timer = 0L; // Placeholder for timerfd read
    int periodic_done = 0; // Flag to check if this is a timerfd
    while(1){

        // el->epollfd: epoll instance
        // el->events: array of epoll_event structs
        // el->max_events: how many events to wait for
        // el->timeout: how long to wait (can be infinite or 0)
        events = epoll_wait(el->epollfd, el->events,el->max_events,el->timeout);
        
        if(events<0){
            if(errno == EINTR) continue; 
            // EINTR stands for "Interrupted system call" ->It indicates that a blocking system call was interrupted by a signal.
            rc = -1;
            el->status = errno;
            break;
            //if a real error, save it and exit loop.
        }

        // If the event has EPOLLERR (error) or EPOLLHUP (hang up), or is not readable/writable:
        // Shut down and close the FD
        // Skip this event
        
        for(int i = 0;i<events;i++){
            if((el->events[i].events & EPOLLERR) || (el->events[i].events & EPOLLHUP) ||
            (!(el->events[i].events & EPOLLIN) && !(el->events[i].events & EPOLLOUT))){
                perror("epoll_wait(2)");
                shutdown(el->events[i].data.fd,0);
                close(el->events[i].data.fd);
                el->status = errno;
                continue;
            }
            struct closure* closure = el->events[i].data.ptr;
            //check if its a periodic task
            periodic_done = 0;
            for(int i = 0;i<el->periodic_nr && periodic_done == 0;i++){
                if(el->events[i].data.fd == el->periodic_tasks[i]->timerfd){
                    // struct closure ->encapsultes a function and its context
                    struct closure* c = el->periodic_tasks[i]->closure; // calls its associated closure
                    ssize_t bytes_read = read(el->events[i].data.fd, &timer, 8);
                    if (bytes_read < 0) {
                        perror("read timerfd");
                        // You might want to handle the error appropriately here
                    }
                    c->call(el,c->args);
                    periodic_done = 1; // mark this to stop further processing
                }
            }
            if(periodic_done == 1) continue;
            closure->call(el,closure->args);//If this wasn't a timer, call the generic handler stored in closure.
        }
    }
    return rc;
    //0 if all good
    //-1 if an error happened
}

int evloop_rearm_callback_read(struct evloop* el,struct closure* cb){
    return epoll_mod(el->epollfd,cb->fd,EPOLLIN,cb);
}

int evloop_rearm_callback_write(struct evloop* el,struct closure* cb){
    return epoll_mod(el->epollfd,cb->fd,EPOLLOUT,cb);
}

int evloop_del_callback(struct evloop* el,struct closure* cb){
    return epoll_del(el->epollfd,cb->fd);
}