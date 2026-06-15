#include <stdio.h>
#include <string.h>

#include "test_path.h"

void test_fixture_path(char* dest, size_t length, const char* name) {
	snprintf(dest, length, "%s/tests/fixtures/%s", CAVEX_SOURCE_DIR, name);
}
