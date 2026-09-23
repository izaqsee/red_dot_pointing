#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void redpoint_config_init(void);
void redpoint_config_apply(void);
void redpoint_config_end_boot(void);
void redpoint_status_task(void);
void redpoint_config_cdc_task(void);
#ifdef __cplusplus
}
#endif
