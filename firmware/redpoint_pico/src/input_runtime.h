#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { REDPOINT_CLK = 12, REDPOINT_DATA = 13, REDPOINT_LEFT = 3,
       REDPOINT_MIDDLE = 2, REDPOINT_RIGHT = 4, REDPOINT_LED = 23 };
void redpoint_input_init(void);
void redpoint_input_edge(bool bit, uint32_t now);
void redpoint_input_task(void);
void redpoint_input_config_changed(void);
void redpoint_input_post_flash(void);
uint32_t redpoint_input_dropped(void);
#ifdef __cplusplus
}
#endif
