#include<stdlib.h>
#include<string.h>
#include "mqtt.h"
#include "pack.h"

// static
// Means the function has internal linkage, i.e., it's only accessible in the file where it is defined.
// Prevents other source files from using this function even if they include the header.
// 2. size_t -> return type

static size_t unpack_mqtt_connect(const unsigned char *,union mqtt_header *,union mqtt_packet *);
static size_t unpack_mqtt_publish(const unsigned char *, union mqtt_header *, union mqtt_packet *);
static size_t unpack_mqtt_subscribe(const unsigned char *, union mqtt_header *, union mqtt_packet *);
static size_t unpack_mqtt_unsubscribe(const unsigned char *,union mqtt_header *, union mqtt_packet *);
static size_t unpack_mqtt_ack(const unsigned char *, union mqtt_header *, union mqtt_packet *);
static unsigned char *pack_mqtt_header(const union mqtt_header *);
static unsigned char *pack_mqtt_ack(const union mqtt_header *);
static unsigned char *pack_mqtt_connack(const union mqtt_packet *);
static unsigned char *pack_mqtt_suback(const union mqtt_packet *);
static unsigned char *pack_mqtt_publish(const union mqtt_packet *);


static const int MAX_LEN_BYTES = 4; //Remaining length field on the fixed header can be atmost 4 bytes.

int mqtt_encode_length(unsigned char *buf, size_t len){
    int bytes = 0;
    do{
        if(bytes + 1> MAX_LEN_BYTES){
            return bytes;
        }
        short d = len % 128;
        len /= 128;
        if(len > 0){
            d |= 128;
        }
        buf[bytes++] = d;
    }while(len>0);
    return bytes;
}

//In MQTT 
//the Remaining Length field tells how many bytes follow the fixed header. This field uses a variable-length encoding.
unsigned long long mqtt_decode_length(const unsigned char **buf){
    char c; // holds each byte from the buffer.
    int multiplier = 1;
    unsigned long long value = 0LL; //stores the decoded result.
    do{
        c = **buf;
        value += (c & 127)*multiplier; //c & 127 removes the MSB (bit 7), leaving only the lower 7 bits (data bits).
        // Multiply by current multiplier and add to result
        // This reassembles the full integer in chunks of 7 bits.
        multiplier *= 128;
        (*buf)++;
    }while((c & 128)!=0); //MSB --> 0 , meaning current byte is last
    return value;
}


//MQTT Unpacking Functions

static size_t unpack_mqtt_connect(const unsigned char *buf,union mqtt_header *hdr, union mqtt_packet *pkt){
    struct mqtt_connect connect = { .header = *hdr }; //Designated initializer — set the header field to the value pointed to by hdr
    pkt->connect = connect;
    const unsigned char* init = buf;
    size_t len = mqtt_decode_length(&buf); //Second byte of the fixed header, contains the length of remaining bytes of the connect packet
    //For now we ignore checks on protocol name and reserved bits, just skip to the 8th byte
    buf = init + 8;
    //Read variable header byte flags
    pkt->connect.byte = unpack_u8((const uint8_t **)&buf); //buf is cast to const uint8_t ** so the function can update the pointer 
    //Reads 2 bytes (a 16-bit integer) and stores it in keepalive.
    pkt->connect.payload.keepalive = unpack_u16((const uint8_t **)&buf); //Read keepalive MSB and LSB 
    uint16_t cid_len = unpack_u16((const uint8_t **)&buf);// reading the length of the client ID string encoded as 2 bytes 
    
    if(cid_len>0){ //Read the client id
        pkt->connect.payload.client_id = malloc(cid_len+1);
        unpack_bytes((const uint8_t **)&buf,cid_len,pkt->connect.payload.client_id);
    }

    //Read the will topic and message if will is set on flags
    if(pkt->connect.bits.will == 1){
        unpack_string16(&buf,&pkt->connect.payload.will_topic);
        unpack_string16(&buf,&pkt->connect.payload.will_message);
    }
    
    // Parse Username & Password
    //Read the username if username flag is set 
    if(pkt->connect.bits.username == 1) unpack_string16(&buf,&pkt->connect.payload.username);
    if(pkt->connect.bits.password == 1) unpack_string16(&buf,&pkt->connect.payload.password);

    return len; //Returns how many bytes were used in the variable header and payload
}
