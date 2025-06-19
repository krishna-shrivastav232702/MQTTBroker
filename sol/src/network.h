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

#endif

