#pragma once
#include "lwip/pbuf.h"
#define NO_SYS 1
#define LWIP_HTTPD_SUPPORT_POST 1
#define LWIP_HTTPD_CUSTOM_FILES 1
extern "C" {
err_t httpd_post_begin(void *, const char *, const char *, u16_t, int, char *, u16_t, u8_t *);
err_t httpd_post_receive_data(void *, pbuf *);
void httpd_post_finished(void *, char *, u16_t);
}
