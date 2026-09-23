/* Standard lwIP httpd ROM filesystem: a diagnostic page, no Configurator/API. */
#include "lwip/apps/fs.h"
static const unsigned char status_path[] = "/index.html";
static const unsigned char status_page[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
  "Cache-Control: no-store\r\n\r\n"
  "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
  "<title>RedPoint RevA</title><h1>RedPoint RevA - Milestone A</h1>"
  "<p>USB NCM + CDC Serial + HID Mouse + HID Keyboard</p>"
  "<p>169.254.7.1/16 - no DHCP or gateway</p></html>";
static const struct fsdata_file status_file = {
  .next = NULL, .name = status_path, .data = status_page,
  .len = sizeof(status_page) - 1, .flags = FS_FILE_FLAGS_HEADER_INCLUDED
};
#define FS_ROOT (&status_file)
#define FS_NUMFILES 1
