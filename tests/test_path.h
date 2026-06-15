#ifndef TEST_PATH_H
#define TEST_PATH_H

#include <stddef.h>

#ifndef CAVEX_SOURCE_DIR
#define CAVEX_SOURCE_DIR "."
#endif

void test_fixture_path(char* dest, size_t length, const char* name);

#endif
