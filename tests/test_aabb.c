#include "aabb.h"
#include "harness.h"

static struct AABB unit_cube(void) {
	struct AABB box;

	aabb_setsize(&box, 1.0F, 1.0F, 1.0F);
	return box;
}

TEST(aabb_setsize_translate) {
	struct AABB box;

	aabb_setsize_centered(&box, 2.0F, 4.0F, 2.0F);
	ASSERT_NEAR(box.x1, -1.0F, 0.001F);
	ASSERT_NEAR(box.x2, 1.0F, 0.001F);
	ASSERT_NEAR(box.y2, 2.0F, 0.001F);

	aabb_translate(&box, 3.0F, -1.0F, 0.5F);
	ASSERT_NEAR(box.x1, 2.0F, 0.001F);
	ASSERT_NEAR(box.z2, 1.5F, 0.001F);
}

TEST(aabb_intersection_overlap) {
	struct AABB a = unit_cube();
	struct AABB b = unit_cube();
	struct AABB c = unit_cube();

	aabb_translate(&b, 0.5F, 0.0F, 0.0F);
	ASSERT(aabb_intersection(&a, &b));

	aabb_translate(&c, 2.0F, 0.0F, 0.0F);
	ASSERT(!aabb_intersection(&a, &c));
}

TEST(aabb_point_inside) {
	struct AABB box = unit_cube();

	ASSERT(aabb_intersection_point(&box, 0.5F, 0.5F, 0.5F));
	ASSERT(!aabb_intersection_point(&box, 2.0F, 0.5F, 0.5F));
}

TEST(aabb_ray_hit_left) {
	struct AABB box = unit_cube();
	struct ray r = {.x = -1.0F, .y = 0.5F, .z = 0.5F, .dx = 1.0F, .dy = 0.0F, .dz = 0.0F};
	enum side hit = SIDE_MAX;

	ASSERT(aabb_intersection_ray(&box, &r, &hit));
	ASSERT_EQ(hit, SIDE_LEFT);
}

TEST(aabb_ray_hit_top) {
	struct AABB box = unit_cube();
	struct ray r = {.x = 0.5F, .y = 2.0F, .z = 0.5F, .dx = 0.0F, .dy = -1.0F, .dz = 0.0F};
	enum side hit = SIDE_MAX;

	ASSERT(aabb_intersection_ray(&box, &r, &hit));
	ASSERT_EQ(hit, SIDE_TOP);
}

TEST(aabb_ray_hit_front) {
	struct AABB box = unit_cube();
	struct ray hit = {.x = 0.5F, .y = 0.5F, .z = -1.0F, .dx = 0.0F, .dy = 0.0F, .dz = 1.0F};
	struct ray miss = {.x = -1.0F, .y = 2.0F, .z = 0.5F, .dx = 1.0F, .dy = 0.0F, .dz = 0.0F};
	struct ray parallel = {.x = 0.5F, .y = 2.0F, .z = 0.5F, .dx = 1.0F, .dy = 0.0F, .dz = 0.0F};
	enum side side = SIDE_MAX;

	ASSERT(aabb_intersection_ray(&box, &hit, &side));
	ASSERT_EQ(side, SIDE_FRONT);
	ASSERT(!aabb_intersection_ray(&box, &miss, NULL));
	ASSERT(!aabb_intersection_ray(&box, &parallel, NULL));
}

const test_entry_t g_tests_aabb[] = {
	{"aabb_setsize_translate", test_aabb_setsize_translate},
	{"aabb_intersection_overlap", test_aabb_intersection_overlap},
	{"aabb_point_inside", test_aabb_point_inside},
	{"aabb_ray_hit_left", test_aabb_ray_hit_left},
	{"aabb_ray_hit_top", test_aabb_ray_hit_top},
	{"aabb_ray_hit_front", test_aabb_ray_hit_front},
};

const size_t g_tests_aabb_count
	= sizeof(g_tests_aabb) / sizeof(g_tests_aabb[0]);
