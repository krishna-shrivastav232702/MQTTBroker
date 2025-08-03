#ifndef NETWORK_H
#define NETWORK_H

#include<stdio.h>
#include<stdint.h>
#include<sys/types.h>
#include "util.h"

#define UNIX 0
#define INET 1

int set_nonblocking(int);
int set_tcp_nodelay(int);

// auxillary function for creating epoll server
int create_and_bind(const char* , const char*, int);

// create a non blocking socket and make it listen on the specified address and port
int make_listen(const char*,const char*, int);


int accept_connection(int); // accept connection and add it to right epoll File descriptor

// I/O Management functions

ssize_t send_bytes(int , const unsigned char* , size_t); //used to send all bytes out at once in while loop till no bytes left

ssize_t recv_bytes(int, unsigned char* ,size_t); //read an arbitrary number of bytes in a while loop

struct evloop {
    int epollfd;
    int max_events;
    int timeout;
    int status;
    struct epoll_event* events;
    int periodic_maxsize;
    int periodic_nr;
    struct{
        int timerfd;
        struct closure* closure;
    } **periodic_tasks;
}evloop;

typedef void callback(struct evloop*, void*);

struct closure {
    int fd;
    void* obj;
    void* args;
    char closure_id[UUID_LEN];
    struct bytestring* payload;
    callback* call;
};


struct evloop* evloop_create(int , int);
void evloop_init(struct evloop*,int,int);
void evloop_free(struct evloop*);

int evloop_wait(struct evloop *);

// register a closure with a function to be executed every time the paired descriptor is re-armed
void evloop_add_callback(struct evloop*,struct closure*);

// register a periodic closure with a function to be executed every defined interval of time 
void evloop_add_periodic_task(struct evloop*, int,unsigned long long, struct closure *);

// unregister a closure by removing the associated descriptor from the EPOLL loop
int evloop_del_callback(struct evloop*, struct closure*);

// rearm the file descriptor associated with the closure for read actions, making the event loop to monitor the callback for reading actions
int evloop_rearm_callback_read(struct evloop*, struct closure*);

int evloop_rearm_callback_write(struct evloop*,struct closure*);

int epoll_add(int,int,int,void*);

int epoll_mod(int, int,int,void*);

int epoll_del(int,int);

#endif

