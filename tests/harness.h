#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST(name) static void test_##name(void)

#define ASSERT(cond)                                                           \
	do {                                                                       \
		if(!(cond)) {                                                          \
			fprintf(stderr, "ASSERT failed: %s\n  at %s:%d in %s\n", #cond,    \
					__FILE__, __LINE__, g_current_test);                       \
			exit(1);                                                           \
		}                                                                      \
	} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NEAR(a, b, eps)                                                 \
	ASSERT(fabs((double)(a) - (double)(b)) < (double)(eps))

extern const char* g_current_test;

typedef struct {
	const char* name;
	void (*fn)(void);
} test_entry_t;

#endif
