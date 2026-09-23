// Standard lwIP httpd extension. Callbacks must run on the config-owning main
// loop (NO_SYS=1), never on an IRQ or a concurrent TCP/IP thread.
#include "config_http.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#if !LWIP_HTTPD_SUPPORT_POST || !LWIP_HTTPD_CUSTOM_FILES || !NO_SYS
#error "RedPoint httpd requires POST, CUSTOM_FILES and NO_SYS=1"
#endif

namespace {
struct Request {
  void *connection;
  size_t expected, received;
  bool ready, inFile, invalid;
  char body[CONFIG_LINE_CAPACITY];
  char wire[768];
  size_t wireLength;
};
Request requests[4] = {};

void uri(char *out, size_t capacity, const char *name) {
  if (capacity) snprintf(out, capacity, "%s", name);
}
// Header lookup uses explicit length: lwIP hands us a non-owning header slice.
int header(const char *data, size_t length, const char *key, char *out, size_t capacity) {
  size_t keyLen = strlen(key);
  bool found = false;
  for (size_t i = 0; i < length;) {
    size_t end = i;
    while (end < length && data[end] && data[end] != '\r' && data[end] != '\n') ++end;
    if (end == i && data[i] == 0) break;
    if (end - i > keyLen && data[i + keyLen] == ':') {
      bool match = true;
      for (size_t j = 0; j < keyLen; ++j)
        if (tolower(static_cast<unsigned char>(data[i+j])) != tolower(key[j])) match = false;
      if (match) {
        if (found) return -1; // Duplicate identity/content headers are malformed.
        found = true;
        size_t first = i + keyLen + 1, last = end;
        while (first < last && (data[first] == ' ' || data[first] == '\t')) ++first;
        while (last > first && (data[last-1] == ' ' || data[last-1] == '\t')) --last;
        if (last - first >= capacity) return -1;
        memcpy(out, data + first, last - first); out[last-first] = 0;
      }
    }
    i = end;
    while (i < length && (data[i] == '\r' || data[i] == '\n')) ++i;
  }
  return found;
}
void response(Request &r, int status, const char *body, size_t length) {
  const char *reason = status == 200 ? "OK" : status == 413 ? "Payload Too Large" :
    status == 503 ? "Service Unavailable" : "Bad Request";
  const int n = snprintf(r.wire, sizeof(r.wire),
    "HTTP/1.0 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %u\r\n\r\n",
    status, reason, static_cast<unsigned>(length));
  memcpy(r.wire + n, body, length);
  r.wireLength = n + length;
  r.ready = true;
}
Request *find(void *connection) {
  for (auto &r : requests) if (r.connection == connection) return &r;
  return nullptr;
}
const char bad[] = "HTTP/1.0 400 Bad Request\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: 12\r\n\r\nBad request\n";
const char large[] = "HTTP/1.0 413 Payload Too Large\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: 15\r\n\r\nBody too large\n";
const char busy[] = "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: 5\r\n\r\nBusy\n";
const char method[] = "HTTP/1.0 405 Method Not Allowed\r\nAllow: POST\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: 9\r\n\r\nUse POST\n";
}

extern "C" err_t httpd_post_begin(void *connection, const char *path, const char *headers,
    u16_t headerLength, int contentLength, char *responseUri, u16_t uriLength, u8_t *autoWindow) {
  uri(responseUri, uriLength, "/_error/400");
  if (strcmp(path, "/api/command") || contentLength <= 0) return ERR_ARG;
  if (contentLength > static_cast<int>(CONFIG_LINE_CAPACITY)) {
    uri(responseUri, uriLength, "/_error/413"); return ERR_ARG;
  }
  char type[64] = {}, transfer[64] = {}, host[128] = {}, origin[144] = {}, lengthText[16] = {};
  if (header(headers, headerLength, "Content-Length", lengthText, sizeof(lengthText)) != 1) return ERR_ARG;
  unsigned parsedLength = 0;
  if (!lengthText[0]) return ERR_ARG;
  for (const char *p = lengthText; *p; ++p) {
    if (*p < '0' || *p > '9' || parsedLength > CONFIG_LINE_CAPACITY) return ERR_ARG;
    parsedLength = parsedLength * 10 + (*p - '0');
  }
  if (parsedLength != static_cast<unsigned>(contentLength)) return ERR_ARG;
  if (header(headers, headerLength, "Content-Type", type, sizeof(type)) != 1 ||
      (strcmp(type, "text/plain") && strcmp(type, "text/plain; charset=utf-8"))) return ERR_ARG;
  if (header(headers, headerLength, "Transfer-Encoding", transfer, sizeof(transfer))) return ERR_ARG;
  // No CORS. Reject cross-origin browser writes even though text/plain is simple.
  const int hasOrigin = header(headers, headerLength, "Origin", origin, sizeof(origin));
  if (hasOrigin < 0) return ERR_ARG;
  if (hasOrigin) {
    if (header(headers, headerLength, "Host", host, sizeof(host)) != 1) return ERR_ARG;
    char expected[144]; snprintf(expected, sizeof(expected), "http://%s", host);
    if (strcmp(origin, expected)) return ERR_ARG;
  }
  for (auto &r : requests) if (!r.connection) {
    r = Request{}; r.connection = connection; r.expected = contentLength;
    *autoWindow = 1;
    return ERR_OK;
  }
  uri(responseUri, uriLength, "/_error/503");
  return ERR_MEM;
}

extern "C" err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
  Request *r = find(connection);
  const bool valid = r && !r->invalid && p->tot_len <= r->expected - r->received;
  if (valid) {
    const u16_t copied = pbuf_copy_partial(p, r->body + r->received, p->tot_len, 0);
    r->received += copied;
    if (copied != p->tot_len) r->invalid = true;
  } else if (r) r->invalid = true;
  pbuf_free(p);
  return valid ? ERR_OK : ERR_ARG;
}

extern "C" void httpd_post_finished(void *connection, char *responseUri, u16_t uriLength) {
  Request *r = find(connection);
  if (!r) return;
  if (r->invalid || r->received != r->expected) {
    // Also called by lwIP on early disconnect; no mutation and no retained slot.
    *r = Request{};
    uri(responseUri, uriLength, "/_error/400");
    return;
  }
  ConfigResponse result;
  const int status = executeConfigHttp(r->body, r->received, result);
  response(*r, status, result.text, result.length);
  if (uriLength) snprintf(responseUri, uriLength, "/_reply/%u", static_cast<unsigned>(r - requests));
}

extern "C" int fs_open_custom(struct fs_file *file, const char *name) {
  const char *data = nullptr; size_t length = 0; Request *owner = nullptr;
  if (!strcmp(name, "/api/command")) { data = method; length = sizeof(method)-1; }
  if (!strcmp(name, "/_error/400")) { data = bad; length = sizeof(bad)-1; }
  if (!strcmp(name, "/_error/413")) { data = large; length = sizeof(large)-1; }
  if (!strcmp(name, "/_error/503")) { data = busy; length = sizeof(busy)-1; }
  if (!strncmp(name, "/_reply/", 8) && name[8] >= '0' && name[8] <= '3' && name[9] == 0) {
    auto &r = requests[name[8]-'0'];
    if (r.ready && !r.inFile) { owner = &r; r.inFile = true; data = r.wire; length = r.wireLength; }
  }
  if (!data) return 0; // Static paths fall through to generated standard fsdata.
  file->data = data; file->len = length; file->index = length;
  file->flags = FS_FILE_FLAGS_HEADER_INCLUDED; file->pextension = owner;
  return 1;
}
extern "C" void fs_close_custom(struct fs_file *file) {
  if (file->pextension) *static_cast<Request *>(file->pextension) = Request{};
}
