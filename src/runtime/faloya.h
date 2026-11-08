













#ifndef FALOYA_H
#define FALOYA_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *top;
} gs_stack;

#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
#pragma pack(pop)




static inline void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

#endif
