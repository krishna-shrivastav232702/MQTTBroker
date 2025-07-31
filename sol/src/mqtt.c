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
static unsigned char* pack_mqtt_header(const union mqtt_header *);
static unsigned char* pack_mqtt_ack(const union mqtt_packet *);
static unsigned char* pack_mqtt_connack(const union mqtt_packet *);
static unsigned char* pack_mqtt_suback(const union mqtt_packet *);
static unsigned char* pack_mqtt_publish(const union mqtt_packet *);


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


static size_t unpack_mqtt_publish(const unsigned char *buf,union mqtt_header *hdr,union mqtt_packet *pkt){
    struct mqtt_publish publish = {.header = *hdr};
    pkt->publish = publish;
    size_t len = mqtt_decode_length(&buf);
    pkt->publish.topiclen = unpack_string16(&buf, &pkt->publish.topic);
    uint16_t message_len = len;
    //read packet id
    if(publish.header.bits.qos > AT_MOST_ONCE){
        pkt->publish.pkt_id = unpack_u16((const uint8_t **)&buf);
        message_len -= sizeof(uint16_t);
    }

    //Message len is calculated subtracting the length of the variable header from the Remaining Length field

    message_len = message_len - (sizeof(uint16_t) + pkt->publish.topiclen);
    pkt->publish.payloadlen = message_len;
    pkt->publish.payload = malloc(message_len+1);
    unpack_bytes((const uint8_t **)&buf,message_len,pkt->publish.payload);
    return len;
}

// subscribe and unsubscribe packets --> same as PUBLISH Packet 
// but for payload they have a list of tuple consisting in a pair (topic,QoS)


static size_t unpack_mqtt_subscribe(const unsigned char* buf, union mqtt_header *hdr,union mqtt_packet *pkt){
    struct mqtt_subscribe subscribe = {.header = *hdr};
    //Second byte of the fixed header, contains the length of remaining bytes of the connect packet
    size_t len = mqtt_decode_length(&buf);
    size_t remaining_bytes = len;
    // read packet id
    subscribe.pkt_id = unpack_u16((const uint8_t **)&buf);
    //unpack_u16() reads two bytes (network → host order) for the Packet Identifier, then advances buf by 2.
    remaining_bytes -= sizeof(uint16_t); //decrement remaining_bytes by 2 to keep count of what’s left.
    
    // from now payload consists of 3-tuples formed by:
    // topic length
    // topic filter(string)
    // qos

    int i = 0;
    while(remaining_bytes > 0){
        // read length bytes of the first topic filter
        remaining_bytes -= sizeof(uint16_t);
        subscribe.tuples = realloc(subscribe.tuples,(i+1)*sizeof(*subscribe.tuples)); //Allocate space for one more tuple
        subscribe.tuples[i].topic_len = unpack_string16(&buf,&subscribe.tuples[i].topic);//Read the topic length & topic string
        remaining_bytes -= subscribe.tuples[i].topic_len;
        
        //Read the QoS byte
        subscribe.tuples[i].qos = unpack_u8((const uint8_t **)&buf);
        len -= sizeof(uint8_t);
        i++; 
    }
    //Store how many tuples we parsed in tuples_len.
    // Copy our local subscribe struct into the output union pkt->subscribe
    subscribe.tuples_len = i;
    pkt->subscribe = subscribe;
    return len;
}


//MQTT packet structure
// Fixed Header
//    +-- Packet Type & Flags (1 byte)
//    +-- Remaining Length (1–4 bytes)
// Variable Header
//    +-- Packet ID (2 bytes)
// Payload
//    +-- Repeated topic filters


//from a raw byte buffer (buf) into a structured format (mqtt_packet union
//size_t return length of the remaining bytes
static size_t unpack_mqtt_unsubscribe(const unsigned char *buf, union mqtt_header *hdr, union mqtt_packet *pkt){
    struct mqtt_unsubscribe unsubscribe = { .header = *hdr };
    size_t len = mqtt_decode_length(&buf); //which tells how many bytes come after the fixed header.
    size_t remaining_bytes = len;
    // read packet id
    unsubscribe.pkt_id = unpack_u16((const uint8_t **)&buf);
    remaining_bytes -= sizeof(uint16_t);

    //from now payload consist of 2 tuples formed by 
    // topic len
    // topic filter

    //Read Payload (Topic Filters)
    int i = 0;
    while(remaining_bytes > 0){
        remaining_bytes -= sizeof(uint16_t);
        //Each time a new topic is found, the array of topic filters (tuples) must grow
        unsubscribe.tuples = realloc(unsubscribe.tuples, (i+1)*sizeof(*unsubscribe.tuples));
        //That returned length is saved into unsubscribe.tuples[i].topic_len.
        // The actual string is stored in unsubscribe.tuples[i].topic.
        unsubscribe.tuples[i].topic_len = unpack_string16(&buf,&unsubscribe.tuples[i].topic);
        remaining_bytes -= unsubscribe.tuples[i].topic_len;
        i++;
    }

    unsubscribe.tuples_len = i;
    pkt->unsubscribe = unsubscribe;
    return len;
}

static size_t unpack_mqtt_ack(const unsigned char *buf, union mqtt_header *hdr, union mqtt_packet *pkt){
    struct mqtt_ack ack = { .header = *hdr };
    size_t len = mqtt_decode_length(&buf);
    ack.pkt_id = unpack_u16((const uint8_t **)&buf);
    pkt->ack = ack;
    return len;
}

//defining a new type alias
typedef size_t mqtt_unpack_handler(const unsigned char *,union mqtt_header *,union mqtt_packet *);


// Unpack functions mapping unpacking_handlers positioned in the array based on message type

//So unpack_handlers[i] is a pointer to a function that unpacks a specific type of MQTT packet and returns a size_t.
static mqtt_unpack_handler *unpack_handlers[11]={
    NULL,
    unpack_mqtt_connect,
    NULL,
    unpack_mqtt_publish,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_ack,
    unpack_mqtt_subscribe,
    NULL,
    unpack_mqtt_unsubscribe
};

//| Index | MQTT Packet Type | Function Assigned         |
// | ----- | ---------------- | ------------------------- |
// | 0     | Reserved         | `NULL`                    |
// | 1     | CONNECT          | `unpack_mqtt_connect`     |
// | 2     | CONNACK          | `NULL`                    |
// | 3     | PUBLISH          | `unpack_mqtt_publish`     |
// | 4     | PUBACK           | `unpack_mqtt_ack`         |
// | 5     | PUBREC           | `unpack_mqtt_ack`         |
// | 6     | PUBREL           | `unpack_mqtt_ack`         |
// | 7     | PUBCOMP          | `unpack_mqtt_ack`         |
// | 8     | SUBSCRIBE        | `unpack_mqtt_subscribe`   |
// | 9     | SUBACK           | `NULL`                    |
// | 10    | UNSUBSCRIBE      | `unpack_mqtt_unsubscribe` |

// the above implementaion using array eliminates the switch-case and makes the design modular and efficient

int unpack_mqtt_packet(const unsigned char *buf,union mqtt_packet *pkt){
    int rc = 0;
    unsigned char type = *buf;
    union mqtt_header header = {
        .byte = type
    };
    if(header.bits.type == DISCONNECT || header.bits.type == PINGREQ || header.bits.type == PINGRESP)
        pkt->header = header;
    else
        rc = unpack_handlers[header.bits.type](++buf,&header,pkt);
    return rc;
}

// struct mqtt_ack *mqtt_packet_ack(unsigned char, unsigned short) will be used to build:
// PUBACK
// PUBREC
// PUBREL
// PUBCOMP
// UNSUBACK

// MQTT Packets Building Functions

union mqtt_header* mqtt_packet_header(unsigned char byte){
    static union mqtt_header header;
    header.byte = byte;
    return &header;
}

struct mqtt_ack* mqtt_packet_ack(unsigned char byte, unsigned short pkt_id){
    static struct mqtt_ack ack;
    ack.header.byte = byte;
    ack.pkt_id = pkt_id;
    return &ack;
}

struct mqtt_connack* mqtt_packet_connack(unsigned char byte,unsigned char cflags, unsigned char rc){
    static struct mqtt_connack connack;
    connack.header.byte = byte;
    connack.byte = cflags;
    connack.rc = rc;
    return &connack;
}

struct mqtt_suback* mqtt_packet_suback(unsigned char byte, unsigned short pkt_id,unsigned char *rcs, unsigned short rcslen){
    struct mqtt_suback* suback = malloc(sizeof(*suback));
    suback->header.byte = byte;
    suback->pkt_id = pkt_id;
    suback->rcslen = rcslen;
    suback->rcs = malloc(rcslen);//rcs is a pointer to an array of return codes
    // void *memcpy(void *dest, const void *src, size_t n);
    memcpy(suback->rcs,rcs,rcslen); //You copy the values from the rcs input array into the newly allocated memory.
    return suback;
}


struct mqtt_publish* mqtt_packet_publish(unsigned char byte, 
    unsigned short pkt_id,
    size_t topiclen,
    unsigned char* topic,
    size_t payloadlen,
    unsigned char* payload) {
        struct mqtt_publish* publish = malloc(sizeof(*publish));
        publish->header.byte = byte;
        publish->pkt_id = pkt_id;
        publish->topiclen = topiclen;
        publish->topic = topic;
        publish->payloadlen = payloadlen;
        publish->payload = payload;
        return publish;
}

void mqtt_packet_release(union mqtt_packet* pkt,unsigned type){
    switch(type){
        case CONNECT:
            free(pkt->connect.payload.client_id);
            if(pkt->connect.bits.username == 1) free(pkt->connect.payload.username);
            if(pkt->connect.bits.password == 1) free(pkt->connect.payload.password);
            if(pkt->connect.bits.will == 1){
                free(pkt->connect.payload.will_message);
                free(pkt->connect.payload.will_topic);
            }
            break;
        case SUBSCRIBE:
        case UNSUBSCRIBE:
            for(unsigned i = 0;i<pkt->subscribe.tuples_len;i++)
                free(pkt->subscribe.tuples[i].topic);
            free(pkt->subscribe.tuples);
            break;
        case SUBACK:
            free(pkt->suback.rcs);
            break;
        case PUBLISH:
            free(pkt->publish.topic);
            free(pkt->publish.payload);
            break;
        default:
            break;                            
    }
}

// cleaner code -> you dont have to write that big function signature every time
typedef unsigned char* mqtt_pack_handler(const union mqtt_packet *);


static mqtt_pack_handler* pack_handlers[13] = {
    NULL,
    NULL,
    pack_mqtt_connack,
    pack_mqtt_publish,
    pack_mqtt_ack,
    pack_mqtt_ack,
    pack_mqtt_ack,
    pack_mqtt_ack,
    NULL,
    pack_mqtt_suback,
    NULL,
    pack_mqtt_ack,
    NULL
};

static unsigned char* pack_mqtt_header(const union mqtt_header* hdr){
    unsigned char* packed = malloc(MQTT_HEADER_LEN);
    unsigned char* ptr = packed;
    //pack_u8() writes 1 byte (header.byte) to the memory location pointed to by ptr
    pack_u8(&ptr,hdr->byte);
    mqtt_encode_length(ptr,0);
    return packed;
}

// converts into raw bytes
static unsigned char* pack_mqtt_ack(const union mqtt_packet *pkt){
    unsigned char* packed = malloc(MQTT_ACK_LEN);
    unsigned char* ptr = packed;
    pack_u8(&ptr,pkt->ack.header.byte);
    mqtt_encode_length(ptr,MQTT_HEADER_LEN);
    //MQTT requires you to write a "Remaining Length" byte right after the header
    ptr++;
    pack_u16(&ptr,pkt->ack.pkt_id); //Writes the 2-byte packet ID to the buffer
    return packed; //return the pointer to the start of the packed MQTT ACK packet, ready to be sent
}


static unsigned char* pack_mqtt_connack(const union mqtt_packet* pkt){
    unsigned char* packed = malloc(MQTT_ACK_LEN);
    unsigned char* ptr = packed;
    pack_u8(&ptr,pkt->connack.header.byte);
    mqtt_encode_length(ptr,MQTT_HEADER_LEN);
    ptr++;
    pack_u8(&ptr,pkt->connack.byte);
    pack_u8(&ptr,pkt->connack.rc);
    return packed;
}

static unsigned char* pack_mqtt_suback(const union mqtt_packet *pkt){
    size_t pktlen = MQTT_HEADER_LEN + sizeof(uint16_t) + pkt->suback.rcslen;
    unsigned char* packed = malloc(pktlen+0);
    unsigned char* ptr = packed;
    pack_u8(&ptr,pkt->suback.header.byte);
    size_t len = sizeof(uint16_t)+pkt->suback.rcslen;
    int step = mqtt_encode_length(ptr,len);
    ptr+=step;
    pack_u16(&ptr,pkt->suback.pkt_id);
    for(int i = 0;i<pkt->suback.rcslen;i++)
        pack_u8(&ptr,pkt->suback.rcs[i]);
    return packed;
}

static unsigned char* pack_mqtt_publish(const union mqtt_packet* pkt){
    size_t pktlen = MQTT_HEADER_LEN + sizeof(uint16_t) + pkt->publish.topiclen + pkt->publish.payloadlen;
    size_t len = 0L; // zero, as a long integer literal
    if(pkt->header.bits.qos > AT_MOST_ONCE)
        pktlen += sizeof(uint16_t);
    int remaininglen_offset = 0;
    if((pktlen - 1)>0x200000)
        remaininglen_offset = 3;
    else if ((pktlen - 1)>0x4000)
        remaininglen_offset = 2;
    else if((pktlen - 1)>0x80)
        remaininglen_offset = 1;
    pktlen += remaininglen_offset;
    unsigned char* packed = malloc(pktlen);
    unsigned char* ptr = packed;
    pack_u8(&ptr, pkt->publish.header.byte);
    len += (pktlen - MQTT_HEADER_LEN - remaininglen_offset);
    
    int step = mqtt_encode_length(ptr,len);
    ptr += step;

    pack_u16(&ptr,pkt->publish.topiclen);
    pack_bytes(&ptr,pkt->publish.topic);

    if(pkt->header.bits.qos > AT_MOST_ONCE)
        pack_u16(&ptr,pkt->publish.pkt_id);

    pack_bytes(&ptr,pkt->publish.payload);
    return packed;
}


unsigned char* pack_mqtt_packet(const union mqtt_packet* pkt, unsigned type){
    if(type == PINGREQ || type == PINGRESP)
        return pack_mqtt_header(&pkt->header);
    return pack_handlers[type](pkt);
}