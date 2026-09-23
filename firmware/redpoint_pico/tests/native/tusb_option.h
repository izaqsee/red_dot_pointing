#pragma once
// lwipopts.h only needs LWIP_HIGH_THROUGHPUT from the target configuration.
// USB compiler attributes are irrelevant to a host-only TCP/HTTP test.
#include "tusb_config.h"
