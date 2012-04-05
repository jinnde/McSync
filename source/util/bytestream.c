#include "definitions.h"

bytestream initbytestream(uint32 len) // allocates bytestream, free when done
{
    bytestream b = (bytestream) malloc(sizeof(struct bytestream_struct));
    b->data = (char*) malloc(len);
    b->head = b->data;
    b->len = 0;
    b->streamlen = len;
    return b;
} // initbytestream

void freebytestream(bytestream b)
{
    free(b->data);
    free(b);
    b = NULL;
} // freebytestream

void bytestreaminsert(bytestream b, void *data, int32 len)
{
    // dynamically allocate more memory if needed
    if(b->len + len > b->streamlen) {
        int32 newlen = len + b->streamlen * 2;
        b->data = (char*) realloc(b->data, newlen);
        b->head = b->data + b->len;
        b->streamlen = newlen;
    }
    memcpy(b->head, data, len);
    b->len += len;
    b->head += len;
} // bytestreaminsert
