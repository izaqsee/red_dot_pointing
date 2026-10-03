#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Authoritative GPIO assignment for the 3.3 V evaluation unit.
// Physical button indices stay 0=Left, 1=Middle, 2=Right.
enum { REDPOINT_CLK = 20, REDPOINT_DATA = 21, REDPOINT_LEFT = 4,
       REDPOINT_MIDDLE = 5, REDPOINT_RIGHT = 3, REDPOINT_LED = 23 };
void redpoint_input_init(void);
void redpoint_input_edge(bool bit, uint32_t now);
void redpoint_input_task(void);
void redpoint_input_config_changed(void);
void redpoint_input_post_flash(void);
uint32_t redpoint_input_dropped(void);
#ifdef __cplusplus
}
#endif
