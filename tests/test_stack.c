#include "harness.h"
#include "stack.h"

TEST(stack_push_pop) {
	struct stack stk;
	int values[] = {10, 20, 30};

	stack_create(&stk, 4, sizeof(int));
	stack_push(&stk, &values[0]);
	stack_push(&stk, &values[1]);
	stack_push(&stk, &values[2]);
	ASSERT_EQ(stack_size(&stk), 3);

	int out = 0;
	ASSERT(stack_pop(&stk, &out));
	ASSERT_EQ(out, 30);
	ASSERT(stack_pop(&stk, &out));
	ASSERT_EQ(out, 20);
	ASSERT(stack_pop(&stk, &out));
	ASSERT_EQ(out, 10);
	ASSERT(stack_empty(&stk));

	stack_destroy(&stk);
}

TEST(stack_pop_empty) {
	struct stack stk;
	int value = 0;

	stack_create(&stk, 4, sizeof(int));
	ASSERT(!stack_pop(&stk, &value));
	stack_destroy(&stk);
}

TEST(stack_growth) {
	struct stack stk;
	int value = 0;

	stack_create(&stk, 2, sizeof(int));
	for(int k = 0; k < 5; k++) {
		value = k;
		stack_push(&stk, &value);
	}

	ASSERT_EQ(stack_size(&stk), 5);
	value = 0;
	ASSERT(stack_pop(&stk, &value));
	ASSERT_EQ(value, 4);
	stack_destroy(&stk);
}

TEST(stack_at) {
	struct stack stk;
	int values[] = {1, 2, 3};
	int out = 0;

	stack_create(&stk, 4, sizeof(int));
	for(size_t k = 0; k < 3; k++)
		stack_push(&stk, &values[k]);

	stack_at(&stk, &out, 1);
	ASSERT_EQ(out, 2);
	stack_destroy(&stk);
}

TEST(stack_clear) {
	struct stack stk;
	int value = 42;

	stack_create(&stk, 4, sizeof(int));
	stack_push(&stk, &value);
	stack_clear(&stk);
	ASSERT(stack_empty(&stk));
	stack_destroy(&stk);
}

const test_entry_t g_tests_stack[] = {
	{"stack_push_pop", test_stack_push_pop},
	{"stack_pop_empty", test_stack_pop_empty},
	{"stack_growth", test_stack_growth},
	{"stack_at", test_stack_at},
	{"stack_clear", test_stack_clear},
};

const size_t g_tests_stack_count
	= sizeof(g_tests_stack) / sizeof(g_tests_stack[0]);
