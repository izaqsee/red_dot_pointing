#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void redpoint_config_init(void);
void redpoint_config_cdc_task(void);
uint32_t redpoint_platform_millis(void);
#ifdef __cplusplus
}
#endif
