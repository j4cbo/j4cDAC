#ifndef _ARCH_CC_H
#define _ARCH_CC_H

#include <stdint.h>
#include <serial.h>

/* Because lwIP is written by idiots. */
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int8_t s8_t;
typedef int16_t s16_t;
typedef int32_t s32_t;
typedef uint32_t mem_ptr_t;

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define BYTE_ORDER LITTLE_ENDIAN

#ifndef NULL
#define NULL 0
#endif

static inline uint16_t LWIP_PLATFORM_HTONS(uint16_t in) {
	return (in >> 8) | (in << 8);
}

static inline uint32_t LWIP_PLATFORM_HTONL(uint32_t in) {
	return ((in & 0xff) << 24) |
	       ((in & 0xff00) << 8) |
	       ((in & 0xff0000UL) >> 8) |
	       ((in & 0xff000000UL) >> 24);
}

#define LWIP_PLATFORM_BYTESWAP 1

#define LWIP_PLATFORM_DIAG(x) outputf x
#define LWIP_PLATFORM_ASSERT(x) // dologf("ASSERT FAILED: %s\n", (x));

#endif
