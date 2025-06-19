#define _DEFAULT_SOURCE

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
    if((fd == socket(AF_UNIX, SOCK_STREAM, 0))==-1){
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
    if((sfd == create_and_bind(host,port,socket_family))==-1){
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
    fprintf(stderr,"send(2) - error sending data",strerror(errno)); //  the number in parentheses indicates the section of the manual
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

