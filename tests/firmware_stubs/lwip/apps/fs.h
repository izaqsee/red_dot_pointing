#pragma once
#include "lwip/err.h"
#define FS_FILE_FLAGS_HEADER_INCLUDED 1
struct fs_file { const char *data; int len, index; void *pextension; u8_t flags; };
struct fsdata_file { const fsdata_file *next; const unsigned char *name, *data; int len; u8_t flags; };
extern "C" {
int fs_open_custom(fs_file *, const char *);
void fs_close_custom(fs_file *);
}
