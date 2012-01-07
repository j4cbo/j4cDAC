/* j4cDAC skub allocator
 *
 * Copyright 2011 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <skub.h>
#include <assert.h>
#include <attrib.h>
#include <LPC17xx.h>
#include <core_cm3.h>

/* The heap */
#define SKUB_POOL_FIXED(name, typ, num) \
	static typ skub_pool_##name[num];
#define SKUB_POOL_VAR(sz, count, attr) \
	static uint8_t skub_pool_##sz[sz * count] __attribute__ ((aligned (4))) attr;
#include <skub-zones.h>
#undef SKUB_POOL_FIXED
#undef SKUB_POOL_VAR

/* Bitfields */
struct skub_bitmaps {
#define SKUB_POOL_FIXED(name, typ, num) \
	uint32_t bitmap_##name[(num + 31) / 32];
#define SKUB_POOL_VAR(sz, count, attr) \
	uint32_t bitmap_##sz[(count + 31) / 32];
#include <skub-zones.h>
#undef SKUB_POOL_FIXED
#undef SKUB_POOL_VAR
};

static struct skub_bitmaps skub_bitmaps AHB0;

/* Look up a region by type */
struct skub_pool_info {
	uint8_t *pool;
	uint32_t *bitmask;
	int max;
	int sz;
};

static const struct skub_pool_info skub_pools_fixed[] = {
#define SKUB_POOL_FIXED(name, typ, num) \
	{ (uint8_t *)skub_pool_##name, skub_bitmaps.bitmap_##name, \
	  num, sizeof(typ) },
#define SKUB_POOL_VAR(sz, count, attr)
#include <skub-zones.h>
#undef SKUB_POOL_FIXED
#undef SKUB_POOL_VAR
};

static const struct skub_pool_info skub_pools_var[] = {
#define SKUB_POOL_FIXED(name, typ, num)
#define SKUB_POOL_VAR(sz, count, attr) \
	{ (uint8_t *)skub_pool_##sz, skub_bitmaps.bitmap_##sz, \
	  count, sz },
#include <skub-zones.h>
#undef SKUB_POOL_FIXED
#undef SKUB_POOL_VAR
};

/* Bit-band macros, from ARM */
#define BITBAND_SRAM_REF	0x20000000
#define BITBAND_SRAM_BASE	0x22000000
#define BITBAND_SRAM(a,b) ((BITBAND_SRAM_BASE + ((a)-BITBAND_SRAM_REF)*32 + ((b)*4)))

static void COLD skub_init_pool(const struct skub_pool_info *pool) {
	int i, nbitfields = (pool->max + 31) / 32;

	for (i = 0; i < nbitfields - 1; i++) {
		pool->bitmask[i] = ~0;
	}

	pool->bitmask[nbitfields - 1] = (1 << (pool->max % 32)) - 1;
}

void COLD skub_init(void) {
	int i;
	for (i = 0; i < ARRAY_NELEMS(skub_pools_var); i++) {
		skub_init_pool(&skub_pools_var[i]);
	}

	for (i = 0; i < ARRAY_NELEMS(skub_pools_fixed); i++) {
		skub_init_pool(&skub_pools_fixed[i]);
	}
}

#ifdef PC_BUILD
#define __LDREXW(x) (*(x))
#define __STREXW(v, x) ((*(x) = (v)), 0)
#endif

static void * skub_alloc_from_pool(const struct skub_pool_info * pool) {

	int i, nbitfields = (pool->max + 31) / 32;

	/* Search for a free block */
	for (i = 0; i < nbitfields; i++) {
		while (1) {
			uint32_t bf = __LDREXW(&pool->bitmask[i]);

			/* If there are no bits free, try the next block */
			if (!bf)
				break;

			/* There is a free bit here. */
			int idx = __builtin_ctz(bf);
			bf &= ~(1 << idx);

			/* Attempt to write. If this fails, try again. */
			if (__STREXW(bf, &pool->bitmask[i]))
				continue;

			/* Success! */
			int chunknum = 32 * i + idx;
			return pool->pool + chunknum * pool->sz;
		}
	}

	/* No free blocks. */
	return NULL;
}

void * skub_alloc(enum skub_type region) {
	void *ptr = skub_alloc_from_pool(&skub_pools_fixed[region]);
#ifdef SKUB_SPEW
	outputf("sa %d -> %p", region, ptr);
#endif
	return ptr;
}

int snprintf(char *buf, unsigned int len, const char *fmt, ...);

void * skub_alloc_sz(int size) {
	/* Find a free block */
	char err[24];
	int i;
	void * ret = NULL;

	if (size > skub_pools_var[ARRAY_NELEMS(skub_pools_var) - 1].sz) {
		snprintf(err, sizeof(err), "alloc %d: too big", size);
		serial_send_str(err);
		return NULL;
	}

	for (i = 0; i < ARRAY_NELEMS(skub_pools_var); i++) {
		if (skub_pools_var[i].sz < size)
			continue;

		ret = skub_alloc_from_pool(&skub_pools_var[i]);

		if (ret)
			break;

		snprintf(err, sizeof(err), "alloc %d: oom in %d", size,
			skub_pools_var[i].sz);
		serial_send_str(err);
	}

#ifdef SKUB_SPEW
	outputf("alloc %d: %d %p", size, i, ret);
#endif

	return ret;
}

void skub_free(enum skub_type region, void *ptr) {
	const struct skub_pool_info *pool = &skub_pools_fixed[region];

	int offset = (uint32_t) ptr - (uint32_t) pool->pool;

	ASSERT(offset >= 0);
	ASSERT(offset < pool->sz * pool->max);
	ASSERT_EQUAL(offset % pool->sz, 0);

	int idx = offset / pool->sz;

	/* Bit-band writes are atomic. */
#ifdef PC_BUILD
	pool->bitmask[idx / 32] |= (1 << (idx % 32));
#else
	*(uint32_t *)BITBAND_SRAM((uint32_t) pool->bitmask, idx) = 1;
#endif
}

void skub_free_sz(void *ptr) {
	/* Figure out which block this was allocated in. */
	int i;

	for (i = 0; i < ARRAY_NELEMS(skub_pools_var); i++) {
		const struct skub_pool_info *pool = &skub_pools_var[i];
		int offset = (uint32_t) ptr - (uint32_t) pool->pool;

		if (offset < 0 || offset >= pool->sz * pool->max)
			continue;

		ASSERT_EQUAL(offset % pool->sz, 0);

		int idx = offset / pool->sz;

		/* Bit-band writes are atomic. */
#ifdef PC_BUILD
		pool->bitmask[idx / 32] |= (1 << (idx % 32));
#else
		*(uint32_t *)BITBAND_SRAM((uint32_t) pool->bitmask, idx) = 1;
#endif

		return;
	}

	panic("skub_free_sz: %p not in any pool", ptr);
}
