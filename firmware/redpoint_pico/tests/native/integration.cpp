#include "config_platform.h"
#include "test_platform.h"
#include "input_runtime.h"
#include "config.h"
#include "config_storage.h"
#include "status_led.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "lwip/timeouts.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include "lwip/priv/tcp_priv.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" uint32_t sys_now(void) { return now; }
extern "C" sys_prot_t sys_arch_protect(void) { return 0; }
extern "C" void sys_arch_unprotect(sys_prot_t) {}
static std::string serialIn, serialOut;
static bool serialConnected = true;
bool tud_cdc_connected(void) { return serialConnected; }
uint32_t tud_cdc_write(const void *p, uint32_t n) {
  n = std::min(n, uint32_t(7)); // Deliberate TX backpressure.
  serialOut.append(static_cast<const char *>(p), n); return n;
}
uint32_t tud_cdc_write_flush(void) { return 0; }
uint32_t tud_cdc_available(void) { return (uint32_t)serialIn.size(); }
int32_t tud_cdc_read_char(void) {
  if (serialIn.empty()) return -1;
  unsigned char c = serialIn[0]; serialIn.erase(0, 1); return c;
}
void tud_cdc_read_flush(void) { serialIn.clear(); }
bool tud_cdc_write_clear(void) { serialOut.clear(); return true; }
static std::string serial(const std::string &s) {
  serialIn = s; serialOut.clear();
  for (int i = 0; i < 300; ++i) redpoint_config_cdc_task();
  assert(serialIn.empty()); return serialOut;
}

using Bytes = std::vector<uint8_t>;
static std::deque<Bytes> packets;
static netif network;
static uint16_t port = 10000;
static uint32_t seq, ack;
static unsigned read16(const Bytes &b, size_t i) { return (b[i] << 8) | b[i+1]; }
static uint32_t read32(const Bytes &b, size_t i) { return (read16(b, i) << 16) | read16(b, i+2); }
static void put16(Bytes &b, size_t i, unsigned n) { b[i] = uint8_t(n >> 8); b[i+1] = uint8_t(n); }
static void put32(Bytes &b, size_t i, uint32_t n) { put16(b, i, n >> 16); put16(b, i+2, n & 0xffff); }
static uint16_t checksum(const Bytes &b, size_t start, size_t length, uint32_t sum = 0) {
  for (size_t i = 0; i < length; i += 2)
    sum += (b[start+i] << 8) | (i+1 < length ? b[start+i+1] : 0);
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return uint16_t(~sum);
}
static err_t output(netif *, pbuf *p, const ip4_addr_t *) {
  Bytes bytes(p->tot_len); pbuf_copy_partial(p, bytes.data(), p->tot_len, 0);
  packets.push_back(bytes); return ERR_OK;
}
static err_t initNetwork(netif *n) { n->output = output; n->mtu = 1500; return ERR_OK; }
static void sendPacket(uint8_t flags, const std::string &body = "") {
  Bytes b(40 + body.size(), 0);
  b[0] = 0x45; put16(b, 2, (unsigned)b.size()); b[8] = 64; b[9] = 6;
  b[12] = 169; b[13] = 254; b[14] = 7; b[15] = 2;
  b[16] = 169; b[17] = 254; b[18] = 7; b[19] = 1;
  put16(b, 20, port); put16(b, 22, 80); put32(b, 24, seq); put32(b, 28, ack);
  b[32] = 0x50; b[33] = flags; put16(b, 34, 32768);
  std::copy(body.begin(), body.end(), b.begin() + 40);
  uint32_t pseudo = read16(b, 12) + read16(b, 14) + read16(b, 16) + read16(b, 18) + 6 + (unsigned)b.size() - 20;
  put16(b, 36, checksum(b, 20, b.size()-20, pseudo));
  put16(b, 10, checksum(b, 0, 20));
  pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)b.size(), PBUF_RAM); assert(p);
  assert(pbuf_take(p, b.data(), (u16_t)b.size()) == ERR_OK);
  assert(ip4_input(p, &network) == ERR_OK);
  seq += (uint32_t)body.size() + !!(flags & 2) + !!(flags & 1);
}
static std::string request(const std::string &text, size_t split = std::string::npos) {
  ++port; seq = 100; ack = 0; packets.clear();
  sendPacket(2); // SYN
  assert(!packets.empty());
  auto syn = packets.front(); packets.pop_front();
  assert((syn[33] & 0x12) == 0x12); ack = read32(syn, 24) + 1;
  sendPacket(0x10);
  if (split != std::string::npos) { sendPacket(0x18, text.substr(0, split)); sendPacket(0x18, text.substr(split)); }
  else sendPacket(0x18, text);
  std::string wire; bool fin = false;
  for (int iteration = 0; iteration < 500 && !fin; ++iteration) {
    unsigned count = 0;
    while (!packets.empty()) {
      assert(++count < 1000);
      auto b = packets.front(); packets.pop_front();
      size_t iplen = (b[0] & 15) * 4;
      size_t tcplen = (b[iplen+12] >> 4) * 4;
      size_t length = read16(b, 2) - iplen - tcplen;
      uint32_t remoteSeq = read32(b, iplen + 4);
      if (length && remoteSeq == ack) {
        wire.append((const char *)b.data() + iplen + tcplen, length); ack += (uint32_t)length;
      }
      bool packetFin = (b[iplen+13] & 1) != 0;
      if (packetFin) { ++ack; fin = true; }
      if (length || packetFin) sendPacket(0x10);
    }
    now += 250; sys_check_timeouts();
  }
  assert(fin && !wire.empty());
  sendPacket(0x11); packets.clear(); // Close peer side, avoiding leaked PCBs.
  return wire;
}
static std::string post(const std::string &body, const std::string &extra = "", const std::string &type = "text/plain") {
  std::string head = "POST /api/command HTTP/1.1\r\nHost: 169.254.7.1\r\nOrigin: http://169.254.7.1\r\nContent-Type: " + type +
    "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n";
  return request(head + body, head.size() + body.size()/2);
}
static std::string bodyOf(const std::string &wire) {
  size_t pos = wire.find("\r\n\r\n"); assert(pos != std::string::npos); return wire.substr(pos+4);
}
int main(int argc, char **argv) {
  assert(argc == 3); // Configurator directory, output GET capture.
  runHardwareTests();
  runScrollTests();
  testReset(); lwip_init();
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, 169, 254, 7, 1); IP4_ADDR(&mask, 255, 255, 0, 0); IP4_ADDR(&gw, 0, 0, 0, 0);
  assert(netif_add(&network, &ip, &mask, &gw, nullptr, initNetwork, ip4_input));
  netif_set_default(&network); netif_set_up(&network); netif_set_link_up(&network); httpd_init();
  for (const char *path : {"/", "/index.html", "/app.js", "/shortcuts.js", "/style.css"}) {
    auto wire = request(std::string("GET ") + path + " HTTP/1.1\r\nHost: 169.254.7.1\r\n\r\n");
    assert(wire.find("HTTP/1.0 200 OK") == 0);
    std::ifstream file(std::string(argv[1]) + (std::string(path) == "/" ? "/index.html" : path), std::ios::binary);
    assert(file); std::string asset((std::istreambuf_iterator<char>(file)), {});
    assert(bodyOf(wire) == asset);
  }
  std::cout << "PASS: real lwIP TCP/httpd GET / and all four static assets, byte-exact\n";
  auto get = post("GET");
  if (get.find("200 OK") == std::string::npos) std::cerr << "GET probe failed: " << get << std::endl;
  assert(get.find("200 OK") != std::string::npos);
  auto initial = bodyOf(get); assert(initial.find("@CONFIG {\"ok\":true,\"command\":\"GET\"") == 0);
  std::ofstream(argv[2], std::ios::binary) << initial;
  assert(serial("GET\n") == initial);
  auto ping = bodyOf(post("PING"));
  assert(ping == "@CONFIG {\"ok\":true,\"command\":\"PING\"}\r\n");
  std::ofstream(std::string(argv[2]) + ".ping", std::ios::binary) << ping;
  assert(bodyOf(post("PING")) == serial("PING\n"));
  assert(bodyOf(post("SET invertX 1")).find("\"ok\":true") != std::string::npos);
  assert(config.invertX && configUnsaved());
  redpoint_config_apply(); testPacket(2, 3); assert(mouseReports.back()[1] == -3);
  assert(serial("GET\n") == bodyOf(post("GET")));
  assert(serial("SET rightAction key:03:17\n").find("\"ok\":true") != std::string::npos);
  assert(bodyOf(post("GET")).find("key:03:17") != std::string::npos);
  serial("SET pointerSensitivity 0.5\n"); redpoint_config_apply(); testPacket(2, 4);
  assert(mouseReports.back()[1] == -2);
  const auto beforeSave = config;
  failCommit = true;
  assert(bodyOf(post("SAVE")) == "@CONFIG {\"ok\":false,\"error\":\"SAVE_FAILED\"}\r\n");
  assert(configUnsaved() && equalDeviceConfig(beforeSave, config) && takeConfigFlashWrite());
  assert(serial("SAVE\n").find("SAVE_FAILED") != std::string::npos);
  failCommit = false;
  assert(bodyOf(post("SAVE")).find("\"ok\":true") != std::string::npos);
  assert(!configUnsaved()); redpoint_config_apply();
  redpoint_config_init(); redpoint_config_end_boot(); assert(equalDeviceConfig(beforeSave, config));
  assert(bodyOf(post("SET invertY 9")).find("INVALID_VALUE") != std::string::npos);
  assert(bodyOf(post("RESET")).find("\"ok\":true") != std::string::npos);
  assert(configUnsaved() && equalDeviceConfig(config, DEFAULT_CONFIG));
  assert(bodyOf(post("GET")) == initial);
  std::cout << "PASS: Pico backend HTTP/CDC shared GET/PING/SET/RESET; SAVE_FAILED preserves state/baseline\n";
  for (const auto &bad : {std::string("GET\nSAVE"), std::string("GET\0x", 5), std::string(" \t")})
    assert(post(bad).find("400 Bad Request") != std::string::npos);
  assert(post(std::string(97, 'X')).find("413 Payload Too Large") != std::string::npos);
  assert(post("GET", "", "application/json").find("400 Bad Request") != std::string::npos);
  assert(post("GET", "Content-Length: 3\r\n").find("400 Bad Request") != std::string::npos);
  assert(post("GET", "Origin: http://elsewhere\r\n").find("400 Bad Request") != std::string::npos);
  assert(post("GET", "Transfer-Encoding: chunked\r\n").find("400 Bad Request") != std::string::npos);
  assert(request("GET /api/command HTTP/1.1\r\nHost: 169.254.7.1\r\n\r\n").find("405 Method Not Allowed") != std::string::npos);
  assert(bodyOf(post("GET")) == initial);
  assert(serial(std::string(96, 'X') + "\nGET\n").find("INVALID_LINE") != std::string::npos);
  serialIn = "SET invertX "; redpoint_config_cdc_task();
  serialConnected = false; redpoint_config_cdc_task(); serialConnected = true;
  assert(serial("GET\n") == initial);
  serial("SET invertX 1\n"); redpoint_config_init(); redpoint_config_end_boot();
  assert(equalDeviceConfig(config, beforeSave));
  std::cout << "PASS: malformed POST rejection, recovery, CDC partial writes/disconnect, HTTP/CDC physical effects and persisted reboot\n";
}
