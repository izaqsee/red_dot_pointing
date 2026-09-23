#include "config_serial.h"
#include "config_http.h"
#include "config_storage.h"
#include "status_led.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>

static int freed = 0;
u16_t pbuf_copy_partial(const pbuf *p, void *output, u16_t size, u16_t offset) {
  unsigned copied = 0;
  for (; p && copied < size; p = p->next) {
    for (unsigned i = 0; i < p->len && copied < size; ++i) {
      if (offset) { --offset; continue; }
      static_cast<char *>(output)[copied++] = static_cast<char *>(p->payload)[i];
    }
  }
  return copied;
}
u8_t pbuf_free(pbuf *) { ++freed; return 1; }
static std::string headers(size_t length) {
  return "HTTP/1.1\r\nHost: 169.254.7.1\r\nOrigin: http://169.254.7.1\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(length) + "\r\n";
}
static std::string consume(const char *path) {
  fs_file file{};
  assert(fs_open_custom(&file, path));
  std::string result(file.data, file.len);
  assert(file.index == file.len && file.flags == FS_FILE_FLAGS_HEADER_INCLUDED);
  fs_close_custom(&file);
  return result;
}
static std::string post(const std::string &body) {
  int connection; char uri[64]{}; u8_t window;
  const auto h = headers(body.size());
  const auto begin = httpd_post_begin(&connection, "/api/command", h.data(), h.size(), body.size(), uri, sizeof(uri), &window);
  if (begin == ERR_OK) {
    // Split both callbacks and chained pbufs, like actual TCP delivery.
    size_t cut = body.size()/2;
    pbuf first{nullptr, const_cast<char *>(body.data()), static_cast<u16_t>(cut), static_cast<u16_t>(cut)};
    assert(httpd_post_receive_data(&connection, &first) == ERR_OK);
    const auto rest = static_cast<u16_t>(body.size()-cut);
    pbuf tail{nullptr, const_cast<char *>(body.data()+cut+1), static_cast<u16_t>(rest-1), static_cast<u16_t>(rest-1)};
    pbuf last{&tail, const_cast<char *>(body.data()+cut), rest, 1};
    assert(httpd_post_receive_data(&connection, &last) == ERR_OK);
    httpd_post_finished(&connection, uri, sizeof(uri));
  }
  return consume(uri);
}
void runHttpTests() {
  loadDeviceConfig(config); configSetPersistentBaseline(config);
  statusLedBegin(); statusLedEndBoot();
  ConfigResponse direct;
  for (const std::string command : {"GET", "SET pointerInvertX 1", "SET rightAction key:03:17", "PING", "SAVE", "RESET", "SET nope 1", "SET pointerInvertY 9", "BAD"}) {
    executeConfigCommand(command.data(), command.size(), direct);
    Stream serial; serial.input = command + "\n";
    while (serial.available()) pollConfigSerial(serial);
    assert(serial.output == direct.text); // Exact framing shared with Serial.
    const auto wire = post("\n" + command + "\n");
    assert(wire.find("HTTP/1.0 200 OK") == 0);
    assert(wire.find("Cache-Control: no-store") != std::string::npos);
    assert(wire.substr(wire.find("\r\n\r\n") + 4) == direct.text);
  }
  executeConfigCommand("RESET", 5, direct); takeConfigChange();
  assert(post("SET pointerInvertX 1\n").find("\"ok\":true") != std::string::npos);
  assert(takeConfigChange() && !takeConfigChange() && config.pointerInvertX);
  const auto before = config;
  for (const auto &body : {std::string("SET pointerInvertX 0\nSAVE\n"), std::string("GET\0junk", 8), std::string(" \t\n")}) {
    assert(post(body).find("400 Bad Request") != std::string::npos);
    assert(equalDeviceConfig(before, config));
  }
  assert(post(std::string(97, 'X')).find("413 Payload Too Large") != std::string::npos);
  assert(post("GET\n").find("200 OK") != std::string::npos);
  assert(consume("/api/command").find("405 Method Not Allowed") != std::string::npos);

  int connections[5]; char uri[64]{}; u8_t window;
  auto h = headers(4);
  for (int i = 0; i < 4; ++i)
    assert(httpd_post_begin(&connections[i], "/api/command", h.data(), h.size(), 4, uri, sizeof(uri), &window) == ERR_OK);
  assert(httpd_post_begin(&connections[4], "/api/command", h.data(), h.size(), 4, uri, sizeof(uri), &window) == ERR_MEM);
  assert(consume(uri).find("503") != std::string::npos);
  for (int i = 0; i < 4; ++i) httpd_post_finished(&connections[i], uri, sizeof(uri)); // early disconnect frees all slots
  for (const auto &badHeader : {h + "Origin: http://evil.test\r\n", h + "Content-Length: 9\r\n", h + "Transfer-Encoding: chunked\r\n", std::string("Content-Type: application/json\r\n")})
    assert(httpd_post_begin(&connections[0], "/api/command", badHeader.data(), badHeader.size(), 4, uri, sizeof(uri), &window) != ERR_OK);
  assert(post("GET\n").find("200 OK") != std::string::npos);
  assert(freed > 0);
  std::cout << "PASS: shared Serial/HTTP core, HTTP framing, lwIP POST chunks/cleanup/limits/headers and status\n";
}
