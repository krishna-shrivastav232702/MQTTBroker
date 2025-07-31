#ifndef PACK_H
#define PACK_H

#include<stdio.h>
#include<stdint.h>

uint8_t unpack_u8(const uint8_t **); //uint8_t, uint16_t, uint32_t — crucial for byte-level operations.

uint16_t unpack_u16(const uint8_t **);
uint32_t unpack_u32(const uint8_t **);
uint8_t* unpack_bytes(const uint8_t **,size_t,uint8_t *); //read a defined len of bytes
uint16_t unpack_string16(const uint8_t **buf, uint8_t **dest);// Unpack a string prefixed by its length as a uint16 value

void pack_u8(uint8_t **,uint8_t);

void pack_u16(uint8_t **,uint16_t); //append a uint16_t -> bytes into the bytestring
void pack_u32(uint8_t **,uint32_t); //append a uint32_t -> bytes into the bytestring
void pack_bytes(uint8_t **,uint8_t *); //append len bytes into the bytestring


// bytestring structure -> convenient way of handling byte string data
struct bytestring{
    size_t size;
    size_t last;
    unsigned char* data;
};

struct bytestring* bytestring_create(size_t);
void bytestring_init(struct bytestring*,size_t);
void bytestring_release(struct bytestring*);
void bytestring_reset(struct bytestring*);

#endif