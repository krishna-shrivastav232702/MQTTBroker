#ifndef SEVRER_H
#defint SEVRER_H

//Epoll default settings for concurrent events monitored and timeout
#define EPOLL_MAX_EVENTS 256
#define EPOLL_TIMEOUT -1


// error codes for packet reception , signaling respectively
// client disconnection
// error reading packet
// error packet sent exceeds size defined by configuration


#define ERRCLIENTDC 1
#define ERRPACKETERR 2
#define ERRMAXREQSIZE 3

// return codes of handler functions, signaling if there's data payload to be sent out or
//  if the server just need to re-arm closure for reading incoming bytes

#define REARM_R 0
#define REARM_W 1

int start_server(const char*, const char*);

#endif


