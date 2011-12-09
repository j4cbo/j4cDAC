/* assert()-style macros.
 *
 * Copyright (c) 2009-2010 Jacob Potter.
 *
 *   ASSERT(exp) is a standard assert macro, like assert().
 *
 *   ASSERT_EQUAL(), ASSERT_NOT_EQUAL(), ASSERT_NULL(), and ASSERT_NOT_NULL()
 * are designed to be more helpful; they print the offending values if they
 * fail.
 */

#ifndef ASSERT_H
#define ASSERT_H

#include <panic.h>

#if 1

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define ASSERT(exp)	((void)(likely(exp) ? 0				\
        : panic("%s:%d: Failed assertion \"%s\"", __FILE__, __LINE__, #exp)))

#define ASSERT_EQUAL(e1, e2) do {					\
	int _a_v1 = (e1), _a_v2 = (e2);					\
	if (unlikely(_a_v1 != _a_v2))					\
		panic("%s:%d: Failed assertion \"%s == %s\": %d != %d",	\
			__FILE__, __LINE__, #e1, #e2, _a_v1, _a_v2);	\
	} while(0)

#define ASSERT_EQUAL_P(e1, e2) do {					\
	void * _a_v1 = (e1), * _a_v2 = (e2);				\
	if (unlikely(_a_v1 != _a_v2))					\
		panic("%s:%d: Failed assertion \"%s == %s\": %p != %p",	\
			__FILE__, __LINE__, #e1, #e2, _a_v1, _a_v2);	\
	} while(0)

#define ASSERT_NOT_EQUAL(e1, e2) do {					\
	int _a_v1 = (e1), _a_v2 = (e2);					\
	if (unlikely(_a_v1 == _a_v2))					\
		panic("%s:%d: Failed assertion \"%s != %s\": both %d",	\
			__FILE__, __LINE__, #e1, #e2, _a_v1);		\
	} while(0)

#define ASSERT_NULL(e) do {						\
	void * _a_p = (e);						\
	if (unlikely(_a_p != NULL))					\
		panic("%s:%d: Failed assertion \"%s == NULL\": got %p",	\
			__FILE__, __LINE__, #e, _a_p);			\
	} while(0)

#define ASSERT_NOT_NULL(e) do {						\
	void * _a_p = (e);						\
	if (unlikely(_a_p == NULL))					\
		panic("%s:%d: Failed assertion \"%s != NULL\"",		\
			__FILE__, __LINE__, #e);			\
	} while(0)

#define ASSERT_BIT(e) do {						\
	void * _a_p = (e);						\
	if (unlikely(_a_p != NULL))					\
		panic("%s:%d: Failed assertion \"%s != NULL\"",		\
			__FILE__, __LINE__, #e);			\
	} while(0)

#define ASSERT_OK(e)	ASSERT_EQUAL(e, 0)

#else

#define ASSERT(exp)
#define ASSERT_EQUAL(e1, e2)
#define ASSERT_EQUAL_P(e1, e2)
#define ASSERT_NOT_EQUAL(e1, e2)
#define ASSERT_NULL(e)
#define ASSERT_NOT_NULL(e)
#define ASSERT_BIT(e)

#endif

#endif
