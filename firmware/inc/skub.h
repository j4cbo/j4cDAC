#ifndef SKUB_H
#define SKUB_H

#include <stdint.h>

typedef struct { uint8_t x[1536]; } __attribute__ ((aligned (4))) u1536;

/* Include skub-zones.h once, so any headers it needs are brought in */
#define SKUB_POOL_FIXED(name, typ, num)
#define SKUB_POOL_VAR(sz, num)
#include <skub-zones.h>
#undef SKUB_POOL_FIXED

/* Region types */
enum skub_type {
#define SKUB_POOL_FIXED(name, typ, num) SKUB_##name,
#include <skub-zones.h>
#undef SKUB_POOL_FIXED
#undef SKUB_POOL_VAR
	SKUB_TYPE_MAX
};

void skub_init(void);

void * skub_alloc(enum skub_type region);
void skub_free(enum skub_type region, void * ptr);

void * skub_alloc_sz(int sz);
void skub_free_sz(void * ptr);

#endif
