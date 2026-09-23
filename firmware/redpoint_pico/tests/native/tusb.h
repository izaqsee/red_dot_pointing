#pragma once
// Only the TinyUSB FIFO boundary is mocked; config_cdc.cpp is production code.
#include <stdint.h>
#include <stdbool.h>
bool tud_cdc_connected(void);
uint32_t tud_cdc_write(const void *, uint32_t);
uint32_t tud_cdc_write_flush(void);
uint32_t tud_cdc_available(void);
int32_t tud_cdc_read_char(void);
void tud_cdc_read_flush(void);
bool tud_cdc_write_clear(void);
