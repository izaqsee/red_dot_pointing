#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef int sys_prot_t;
#ifdef _MSC_VER
#define PACK_STRUCT_BEGIN __pragma(pack(push, 1))
#define PACK_STRUCT_END __pragma(pack(pop))
#define PACK_STRUCT_STRUCT
#else
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#endif
#define PACK_STRUCT_FIELD(x) x
#define LWIP_PLATFORM_ASSERT(message) do { fprintf(stderr, "%s\n", message); abort(); } while (0)
