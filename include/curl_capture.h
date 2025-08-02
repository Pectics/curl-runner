#ifndef CURL_CAPTURE_H
#define CURL_CAPTURE_H

#include <stdlib.h>
#include <string.h>

struct CaptureBuffer {
    char *data;
    size_t size;
    size_t capacity;
};

static inline void capture_init(struct CaptureBuffer *cap, char *staticbuf, size_t capsize) {
    cap->data = staticbuf;
    cap->size = 0;
    cap->capacity = capsize;
    if(staticbuf && capsize > 0)
        cap->data[0] = '\0';
}

static inline void capture_append(struct CaptureBuffer *cap, const char *msg, size_t len) {
    if(cap->size + len + 1 > cap->capacity) {
        size_t newcap = (cap->size + len + 1) * 2;
        char *newbuf = (char*)realloc(cap->data, newcap);
        if(!newbuf) return;
        cap->data = newbuf;
        cap->capacity = newcap;
    }
    memcpy(cap->data + cap->size, msg, len);
    cap->size += len;
    cap->data[cap->size] = '\0';
}

#endif // CURL_CAPTURE_H