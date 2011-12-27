/* vsnprintf.c
 *
 * Copyright (c) 2011, Jacob Potter
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdarg.h>

#define ALWAYS_INLINE __attribute__((always_inline))

struct context {
	unsigned int state;
	char *buf;
	unsigned int len;
	unsigned int total;
};

enum {
	ST_ZEROPAD  = 0x00000001,
	ST_LONG     = 0x00000002,
	ST_LONGER   = 0x00000004,
	ST_WIDTH    = 0x00000008,
	ST_NEGATIVE = 0x00000010,
	ST_PREC     = 0x00000020,
};

#define PACKWID(c,w)	((c)->state |= (w << 20))
#define WID(c)		(((c)->state >> 20) & 0xFFF)
#define PACKPREC(c,w)	((c)->state |= (w << 12))
#define PREC(c)		(((c)->state >> 12) & 0xFF)

static ALWAYS_INLINE void savechar(struct context *ctx, char c) {
	ctx->total++;
	if (ctx->len <= 0)
		return;

	*(ctx->buf++) = c;
	ctx->len--;
}

static ALWAYS_INLINE int is_digit(char c) {
	return c >= '0' && c <= '9';
}

static ALWAYS_INLINE unsigned int parse_int(const char **p) {
	unsigned int v = 0;
	while (is_digit(**p)) {
		v *= 10;
		v += (**p - '0');
		(*p)++;
	}
	return v;
}

static const char digits[] = "0123456789abcdef";

static ALWAYS_INLINE void
format_int(struct context *ctx, unsigned int base, unsigned long arg) {
	char buf[12]; /* 32 bits = 11 octal digits, leading '-' */
	char *n = buf + sizeof(buf) - 1;
	char *o = n;

	if (!arg) {
		*n = digits[0];
		n--;
	}

	while (arg) {
		*n = digits[arg % base];
		n--;
		arg /= base;
	}

	if (ctx->state & ST_ZEROPAD)
		while ((o - n) < WID(ctx) && n > buf)
			*n-- = '0'; 

	if (ctx->state & ST_NEGATIVE)
		*n-- = '-';

	n++;

	while (n < buf + sizeof(buf))
		savechar(ctx, *n++);
}

int vsnprintf(char *buf, unsigned int len, const char *fmt, va_list va) {
	struct context ctx;
	unsigned int n;
	int i;
	const char *s;

	ctx.buf = buf;
	ctx.len = len;
	ctx.total = 0;
	ctx.state = 0;

	while (*fmt) {
		if (*fmt != '%') {
			savechar(&ctx, *fmt++);
			continue;
		}

		fmt++;
		ctx.state = 0;

		if (*fmt == '0') {
			ctx.state |= ST_ZEROPAD;
			fmt++;
		}

		if (is_digit(*fmt)) {
			PACKWID(&ctx, parse_int(&fmt));
			ctx.state |= ST_WIDTH;
		}

		if (*fmt == '.') {
			fmt++;
			if (is_digit(*fmt))
				PACKPREC(&ctx, parse_int(&fmt));
			else if (*fmt == '*') {
				fmt++;
				PACKPREC(&ctx, va_arg(va, unsigned int));
			}
			ctx.state |= ST_PREC;
		}

		if (*fmt == 'l') {
			fmt++;
			ctx.state |= (*fmt == 'l' ? ST_LONGER : ST_LONG);
		}

		switch (*fmt) {
		case 's':
			s = va_arg(va, const char *);
			n = PREC(&ctx);
			while ((!(ctx.state & ST_PREC) || n) && *s) {
				savechar(&ctx, *s++);
				n--;
			}
			break;

		case 'p':
			ctx.state |= ST_ZEROPAD;
			PACKWID(&ctx, 2 * sizeof(void*));
			format_int(&ctx, 16, (unsigned long)va_arg(va, void *));
			break;

		case 'd':
			i = va_arg(va, int);
			if (i < 0) {
				ctx.state |= ST_NEGATIVE;
				i = -i;
			}
			format_int(&ctx, 10, i);
			break;

		case 'u':
			format_int(&ctx, 10, va_arg(va, unsigned int));
			break;

		case 'o':
			format_int(&ctx, 8, va_arg(va, unsigned int));
			break;

		case 'x':
			format_int(&ctx, 16, va_arg(va, unsigned int));
			break;

		case 'c':
			savechar(&ctx, va_arg(va, int));
			break;

		case '%': 
			savechar(&ctx, '%');
		}

		fmt++;
	}

	if (ctx.len == 0) {
		buf[len- 1] = '\0';
	} else {
		*ctx.buf = '\0';
		ctx.total++;
	}

	return ctx.total;
}

int snprintf(char *buf, unsigned int len, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int ret = vsnprintf(buf, len, fmt, va);
	va_end(va);
	return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int ret = vsnprintf(buf, ~0, fmt, va);
	va_end(va);
	return ret;
}

/* atoi
 *
 * Replace built-in atoi with a simple, ascii-only implementation.
 */
int atoi(const char *c) {
        if (!c) return 0;
        /* Drop leading whitespace */
        while (*c == ' ' || *c == '\t') c++;

        /* Handle sign */
        int sign = 1;
        if (*c == '-') { sign = -1; c++; }
        else if (*c == '+') c++;

        int res = 0;
        while (*c >= '0' && *c <= '9')
                res = res * 10 + (*c++ - '0');

        return res * sign;
}
