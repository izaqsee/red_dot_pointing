#pragma once
#include "config.h"
// HTTP framing validation only. Protocol errors still have status 200.
int executeConfigHttp(const char *body, size_t length, ConfigResponse &response);
