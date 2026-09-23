#pragma once
#include "err.h"
struct pbuf { pbuf *next; void *payload; u16_t tot_len, len; };
u16_t pbuf_copy_partial(const pbuf *, void *, u16_t, u16_t);
u8_t pbuf_free(pbuf *);
