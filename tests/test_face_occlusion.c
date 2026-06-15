#include "face_occlusion.h"
#include "harness.h"

TEST(face_occlusion_empty_passes) {
	struct face_occlusion custom = {{0}};

	ASSERT(face_occlusion_test(face_occlusion_empty(), face_occlusion_full()));
	ASSERT(face_occlusion_test(&custom, face_occlusion_full()));
}

TEST(face_occlusion_full_blocks) {
	ASSERT(!face_occlusion_test(face_occlusion_full(), face_occlusion_full()));
}

TEST(face_occlusion_partial) {
	struct face_occlusion partial = *face_occlusion_rect(8);
	struct face_occlusion other = *face_occlusion_rect(4);

	ASSERT(face_occlusion_test(&partial, &other));
	ASSERT(face_occlusion_rect(0) == face_occlusion_empty());
	ASSERT(face_occlusion_rect(16) == face_occlusion_full());
	ASSERT(face_occlusion_rect(8)->mask[7] != 0);
}

const test_entry_t g_tests_face_occlusion[] = {
	{"face_occlusion_empty_passes", test_face_occlusion_empty_passes},
	{"face_occlusion_full_blocks", test_face_occlusion_full_blocks},
	{"face_occlusion_partial", test_face_occlusion_partial},
};

const size_t g_tests_face_occlusion_count
	= sizeof(g_tests_face_occlusion) / sizeof(g_tests_face_occlusion[0]);
