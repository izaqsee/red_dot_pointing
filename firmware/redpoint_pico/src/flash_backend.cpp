#include "platform_io.h"
#include "flash_layout.h"
#include "config_record.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

static_assert(REDPOINT_CONFIG_OFFSET % FLASH_SECTOR_SIZE == 0, "Unaligned config sector");
static_assert(REDPOINT_CONFIG_OFFSET + FLASH_SECTOR_SIZE == REDPOINT_FLASH_BYTES, "Config must be final sector");
static_assert(CONFIG_RECORD_SIZE <= FLASH_PAGE_SIZE, "Record exceeds program page");
static_assert(PICO_FLASH_SIZE_BYTES == REDPOINT_FLASH_BYTES, "SDK physical Flash size mismatch");
extern "C" char __flash_binary_end;
namespace {
bool available;
alignas(4) uint8_t page[FLASH_PAGE_SIZE]; // SRAM, never XIP data during program
void __not_in_flash_func(writePage)(void *) {
  flash_range_erase(REDPOINT_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(REDPOINT_CONFIG_OFFSET, page, sizeof(page));
}
}
extern "C" bool redpoint_storage_init(void) {
  available = false;
  if ((uintptr_t)&__flash_binary_end > XIP_BASE + REDPOINT_CONFIG_OFFSET) return false;
  // RDID at boot only, before PS/2 and USB initialization; capacity byte is log2(bytes).
  const uint8_t tx[4] = {0x9f, 0, 0, 0}; uint8_t rx[4] = {};
  uint32_t irq = save_and_disable_interrupts();
  flash_do_cmd(tx, rx, sizeof(tx));
  restore_interrupts(irq);
  available = rx[1] != 0 && rx[1] != 255 && rx[3] == 24; // 2^24 = 16 MiB
  return available;
}
extern "C" bool redpoint_storage_read(uint8_t *data, size_t size) {
  if (!available || size > FLASH_PAGE_SIZE) return false;
  memcpy(data, (const void *)(XIP_BASE + REDPOINT_CONFIG_OFFSET), size);
  return true;
}
extern "C" bool redpoint_storage_commit(const uint8_t *data, size_t size) {
  if (!available || size != CONFIG_RECORD_SIZE) return false;
  memset(page, 255, sizeof(page)); memcpy(page, data, size);
  // Only core 0 runs this firmware. core 1 is never launched; SDK masks core 0
  // interrupts, and its SRAM flash functions re-enable XIP before returning.
  if (flash_safe_execute(writePage, nullptr, 1000) != PICO_OK) return false;
  // Verify the entire programmed page, followed by record/CRC validation in storage.
  return memcmp(page, (const void *)(XIP_BASE + REDPOINT_CONFIG_OFFSET), sizeof(page)) == 0;
}
