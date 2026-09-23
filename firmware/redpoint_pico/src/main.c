/* Network glue adapted from Peter Lawrence's TinyUSB example (2020).
 * See LICENSE.reference. All lwIP and USB work runs on core 0, without an RTOS. */
#include "bsp/board_api.h"
#include "tusb.h"
#include "hid.h"
#include "config_platform.h"
#include "platform_io.h"
#include "input_runtime.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "lwip/apps/httpd.h"

static struct netif network;
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6a, 0x96, 0x00};

static err_t link_output(struct netif *netif, struct pbuf *p) {
  (void)netif;
  if (!tud_ready()) return ERR_USE;
  // Let the main loop service all four functions even under network backpressure.
  if (!tud_network_can_xmit(p->tot_len)) return ERR_MEM;
  tud_network_xmit(p, 0);
  return ERR_OK;
}
static err_t network_init(struct netif *netif) {
  netif->name[0] = 'r'; netif->name[1] = 'p';
  netif->mtu = 1500;
  netif->hwaddr_len = 6;
  memcpy(netif->hwaddr, tud_network_mac_address, 6);
  netif->hwaddr[5] ^= 1; // Device MAC differs from the host-side MAC string.
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  netif->linkoutput = link_output;
  netif->output = etharp_output;
  return ERR_OK;
}
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  if (size) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    // Drop exhausted frames and renew reception, rather than wedging the RX queue.
    if (p) {
      if (pbuf_take(p, src, size) != ERR_OK || network.input(p, &network) != ERR_OK)
        pbuf_free(p);
    }
  }
  tud_network_recv_renew();
  return true;
}
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void)arg;
  struct pbuf *p = ref;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}
int main(void) {
  board_init();
  redpoint_hardware_init();
  redpoint_config_init();
  lwip_init();
  ip4_addr_t ip, mask, gateway;
  IP4_ADDR(&ip, 169, 254, 7, 1);
  IP4_ADDR(&mask, 255, 255, 0, 0);
  IP4_ADDR(&gateway, 0, 0, 0, 0);
  if (!netif_add(&network, &ip, &mask, &gateway, NULL, network_init, ethernet_input))
    return 1;
  netif_set_default(&network);
  netif_set_up(&network);
  netif_set_link_up(&network);
  tusb_rhport_init_t init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
  tusb_init(BOARD_TUD_RHPORT, &init);
  board_init_after_tusb();
  redpoint_input_init();
  redpoint_input_irq_enable();
  httpd_init();
  redpoint_config_end_boot();
  while (true) {
    tud_task();
    sys_check_timeouts();
    redpoint_config_cdc_task();
    redpoint_config_apply();
    redpoint_input_task();
    redpoint_hid_task();
    redpoint_status_task();
  }
}
sys_prot_t sys_arch_protect(void) { return 0; }
void sys_arch_unprotect(sys_prot_t value) { (void)value; }
uint32_t sys_now(void) { return tusb_time_millis_api(); }
uint32_t redpoint_platform_millis(void) { return tusb_time_millis_api(); }
