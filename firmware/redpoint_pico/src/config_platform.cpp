#include "config_platform.h"
#include "config.h"
#include "config_storage.h"
#include "status_led.h"
#include "input_runtime.h"

extern "C" void redpoint_config_init(void) {
  statusLedBegin();
  loadDeviceConfig(config);
  configSetPersistentBaseline(config);
  (void)takeConfigChange();
}
extern "C" void redpoint_config_apply(void) {
  if (takeConfigChange()) redpoint_input_config_changed();
  if (takeConfigFlashWrite()) redpoint_input_post_flash();
}
extern "C" void redpoint_config_end_boot(void) { statusLedEndBoot(); }
extern "C" void redpoint_status_task(void) { statusLedUpdate(); }
