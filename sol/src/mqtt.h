#ifndef MQTT_H
#define MQTT_H

#include<stdio.h>

#define MQTT_HEADER_LEN 2
#define MQTT_ACK_LEN 4  //refers to fixed sizes of the MQTT Fixed Header and of every type of MQTT ACK packets

//stub bytes useful for generic replies

#define CONNACK_BYTE  0x20 //Acknowledges a CONNECT request from the client.
#define PUBLISH_BYTE  0x30 //Delivers an application message to subscribers.
#define PUBACK_BYTE   0x40 //Acknowledges receipt of a PUBLISH packet
#define PUBREC_BYTE   0x50 //receiver confirms receipt of PUBLISH
#define PUBREL_BYTE   0x60 //sender sends "release"
#define PUBCOMP_BYTE  0x70 //confirms message processing is complete
#define SUBACK_BYTE   0x90 //Acknowledges a SUBSCRIBE request and gives granted QoS levels
#define UNSUBACK_BYTE 0xB0 //Acknowledges an UNSUBSCRIBE request
#define PINGRESP_BYTE 0xD0 


enum packet_type {
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    PUBREC = 5,
    PUBREL = 6,
    PUBCOMP = 7,
    SUBSCRIBE = 8,
    SUBACK = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14
};

enum qos_level {
    AT_MOST_ONCE,
    AT_LEAST_ONCE,
    EXACTLY_ONCE
};


//Why Use a union?

//Sometimes you want to access the entire byte (e.g., sending raw packet).

//Sometimes you want to set/check individual bits for logic.

union mqtt_header {
    unsigned char byte;
    struct {
        unsigned retain : 1;
        unsigned qos : 2;
        unsigned dup :1;
        unsigned type : 4;
    }bits;
};

struct mqtt_connect { // first packet a client sends to a broker 
    union mqtt_header header;
    union {
        unsigned char byte;
        struct {
            int reserved : 1;
            unsigned clean_session: 1;
            unsigned will: 1;
            unsigned will_qos : 2;
            unsigned will_retain: 1;
            unsigned password: 1;
            unsigned username: 1;
        }bits;
    };
    struct {
        unsigned short keepalive;
        unsigned char *client_id;
        unsigned char *username;
        unsigned char *password;
        unsigned char *will_topic;
        unsigned char *will_message;
    }payload;
};

struct mqtt_connack{
    union mqtt_header header;
    union{
        //Use byte to quickly read/write the whole value.
        unsigned char byte;
        struct{
            unsigned session_present: 1;
            unsigned reserved: 7;
        }bits;
    };
    unsigned char rc; //return code -> tells the client if connection was successful or not.
};

struct mqtt_subscribe {
    union mqtt_header header;
    unsigned short pkt_id;
    unsigned short tuples_len; // It stores the number of topic+QoS pairs //how many topics the client is subscribing to
    struct{
        unsigned short topic_len;
        unsigned char *topic;
        unsigned qos;
    }*tuples; //pointer to an array of subscription tuples,
};






