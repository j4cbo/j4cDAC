/* j4cDAC parameter struct
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

#ifndef PARAM_H
#define PARAM_H

#include <stdint.h>
#include <fixpoint.h>

#define PARAM_STRING	-123

typedef struct param_handler {
	const char *address;
	enum {
		PARAM_TYPE_0 = 0,
		PARAM_TYPE_I1 = 1,
		PARAM_TYPE_I2 = 2,
		PARAM_TYPE_I3 = 3,
		PARAM_TYPE_S1 = 42
	} type;
	union {
		void (*f0) (const char *);
		void (*f1) (const char *, int32_t);
		void (*f2) (const char *, int32_t, int32_t);
		void (*f3) (const char *, int32_t, int32_t, int32_t);
		void (*fs) (const char *, const char *);
	};
	enum {
		PARAM_MODE_INT,
		PARAM_MODE_FIXED
	} intmode;
	fixed min;
	fixed max;
} param_handler;

int FPA_param(const volatile param_handler *h, const char *addr, int32_t *params);
void param_invocation_dump(const struct param_handler *h,
	const char *addr, int32_t *params);

extern const volatile param_handler param_handler_table[0];
extern const volatile param_handler param_handler_table_end[0];

int osc_parameter_matches(const char *handler, const char *packet);

#define foreach_matching_handler(h, cmpaddr)				\
	for (h = param_handler_table; h < param_handler_table_end; h++)	\
		if (h->f0 && osc_parameter_matches(h->address, cmpaddr))

#endif
