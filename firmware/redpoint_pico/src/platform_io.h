#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t redpoint_platform_millis(void);
uint32_t redpoint_irq_save(void);
void redpoint_irq_restore(uint32_t state);
bool redpoint_button_pressed(unsigned index);
void redpoint_hardware_init(void);
void redpoint_input_irq_enable(void);
void redpoint_input_irq_clear(void);
bool redpoint_led_init(void);
bool redpoint_led_ready(void);
void redpoint_led_write(uint8_t r, uint8_t g, uint8_t b);
bool redpoint_storage_init(void);
bool redpoint_storage_read(uint8_t *data, size_t size);
bool redpoint_storage_commit(const uint8_t *record, size_t size);
#ifdef __cplusplus
}
#endif
