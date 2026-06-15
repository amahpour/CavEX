#include <errno.h>
#include <string.h>

#include "harness.h"
#include "nbt.h"
#include "test_path.h"

TEST(nbt_parse_level_dat) {
	char path[512];
	nbt_node* root;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);
	ASSERT_EQ(errno, 0);
	nbt_free(root);
}

TEST(nbt_find_spawn_coords) {
	char path[512];
	nbt_node *root, *spawn_x, *spawn_y, *spawn_z;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	spawn_x = nbt_find_by_path(root, ".Data.SpawnX");
	spawn_y = nbt_find_by_path(root, ".Data.SpawnY");
	spawn_z = nbt_find_by_path(root, ".Data.SpawnZ");
	ASSERT(spawn_x != NULL && spawn_y != NULL && spawn_z != NULL);
	ASSERT_EQ(spawn_x->type, TAG_INT);
	ASSERT_EQ(spawn_y->type, TAG_INT);
	ASSERT_EQ(spawn_z->type, TAG_INT);
	ASSERT_EQ(spawn_x->payload.tag_int, 24);
	ASSERT_EQ(spawn_y->payload.tag_int, 64);
	ASSERT_EQ(spawn_z->payload.tag_int, 24);

	nbt_free(root);
}

TEST(nbt_parse_missing_file) {
	nbt_node* root = nbt_parse_path("/tmp/cavex_missing_level_dat_test.dat");

	ASSERT(root == NULL);
	ASSERT_EQ(errno, NBT_EIO);
}

TEST(nbt_parse_truncated) {
	char path[512];
	nbt_node* root;

	test_fixture_path(path, sizeof(path), "nbt_truncated.dat");
	root = nbt_parse_path(path);
	ASSERT(root == NULL);
	ASSERT(errno != 0);
}

TEST(nbt_clone_roundtrip) {
	char path[512];
	nbt_node *root, *clone;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	clone = nbt_clone(root);
	ASSERT(clone != NULL);
	ASSERT(nbt_eq(root, clone));

	nbt_free(clone);
	nbt_free(root);
}

TEST(nbt_util_strings) {
	ASSERT(strcmp(nbt_type_to_string(TAG_INT), "TAG_INT") == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_OK), "No error.") == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_EIO), "IO Error. Nonexistant/corrupt file?")
		   == 0);
}

TEST(nbt_binary_roundtrip) {
	char path[512];
	nbt_node *root, *parsed;
	struct buffer blob;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	blob = nbt_dump_binary(root);
	ASSERT(blob.data != NULL);

	parsed = nbt_parse(blob.data, blob.len);
	ASSERT(parsed != NULL);
	ASSERT(nbt_eq(root, parsed));

	nbt_free(parsed);
	buffer_free(&blob);
	nbt_free(root);
}

TEST(nbt_find_by_name) {
	char path[512];
	nbt_node *root, *data;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	data = nbt_find_by_name(root, "Data");
	ASSERT(data != NULL);
	ASSERT_EQ(data->type, TAG_COMPOUND);

	nbt_free(root);
}

static char* dup_cstr(const char* s) {
	size_t n = strlen(s) + 1;
	char* out = malloc(n);
	memcpy(out, s, n);
	return out;
}

// Hand-builds a TAG_COMPOUND tree containing one node of every scalar and
// array tag type, returning the root. Caller frees with nbt_free.
static nbt_node* build_full_tree(void) {
	static unsigned char bytes[] = {1, 2, 3, 4};
	static int32_t ints[] = {100, 200, 300};

	// NB: TAG_LONG_ARRAY is intentionally excluded here because this (Beta-era)
	// cNBT's nbt_eq has no TAG_LONG_ARRAY case; it is covered separately in
	// nbt_long_array_roundtrip without relying on nbt_eq.
	nbt_node template_children[] = {
		{.type = TAG_BYTE, .name = "b", .payload.tag_byte = 7},
		{.type = TAG_SHORT, .name = "s", .payload.tag_short = 1234},
		{.type = TAG_INT, .name = "i", .payload.tag_int = 567890},
		{.type = TAG_LONG, .name = "l", .payload.tag_long = 1234567890123LL},
		{.type = TAG_FLOAT, .name = "f", .payload.tag_float = 1.5F},
		{.type = TAG_DOUBLE, .name = "d", .payload.tag_double = 2.5},
		{.type = TAG_STRING, .name = "str", .payload.tag_string = "hello"},
		{.type = TAG_BYTE_ARRAY,
		 .name = "ba",
		 .payload.tag_byte_array = {.data = bytes, .length = 4}},
		{.type = TAG_INT_ARRAY,
		 .name = "ia",
		 .payload.tag_int_array = {.data = ints, .length = 3}},
	};
	size_t n = sizeof(template_children) / sizeof(*template_children);

	struct nbt_list* sentinel = malloc(sizeof(struct nbt_list));
	sentinel->data = NULL;
	INIT_LIST_HEAD(&sentinel->entry);

	for(size_t k = 0; k < n; k++) {
		struct nbt_list* item = malloc(sizeof(struct nbt_list));
		item->data = malloc(sizeof(nbt_node));
		*item->data = template_children[k];

		// nbt_free deep-frees names, array data and strings, so everything
		// owned by the tree must live on the heap.
		item->data->name = dup_cstr(template_children[k].name);

		if(template_children[k].type == TAG_STRING) {
			item->data->payload.tag_string = dup_cstr("hello");
		} else if(template_children[k].type == TAG_BYTE_ARRAY) {
			int32_t len = template_children[k].payload.tag_byte_array.length;
			unsigned char* d = malloc(len);
			memcpy(d, template_children[k].payload.tag_byte_array.data, len);
			item->data->payload.tag_byte_array.data = d;
		} else if(template_children[k].type == TAG_INT_ARRAY) {
			int32_t len = template_children[k].payload.tag_int_array.length;
			int32_t* d = malloc(len * sizeof(int32_t));
			memcpy(d, template_children[k].payload.tag_int_array.data,
				   len * sizeof(int32_t));
			item->data->payload.tag_int_array.data = d;
		}

		list_add_tail(&item->entry, &sentinel->entry);
	}

	nbt_node* root = malloc(sizeof(nbt_node));
	root->type = TAG_COMPOUND;
	root->name = dup_cstr("root");
	root->payload.tag_compound = sentinel;
	return root;
}

static bool count_visitor(nbt_node* node, void* aux) {
	(void)node;
	(*(int*)aux)++;
	return true;
}

static bool keep_ints(const nbt_node* node, void* aux) {
	(void)aux;
	return node->type == TAG_INT || node->type == TAG_COMPOUND;
}

static bool keep_all(const nbt_node* node, void* aux) {
	(void)node;
	(void)aux;
	return true;
}

static bool drop_strings(const nbt_node* node, void* aux) {
	(void)aux;
	return node->type != TAG_STRING;
}

TEST(nbt_full_tree_binary_roundtrip) {
	nbt_node* root = build_full_tree();
	ASSERT(root != NULL);

	// every scalar/array tag type is written and read back
	struct buffer blob = nbt_dump_binary(root);
	ASSERT(blob.data != NULL);

	nbt_node* parsed = nbt_parse(blob.data, blob.len);
	ASSERT(parsed != NULL);
	ASSERT(nbt_eq(root, parsed));

	nbt_node* clone = nbt_clone(parsed);
	ASSERT(clone != NULL);
	ASSERT(nbt_eq(parsed, clone));

	nbt_free(clone);
	nbt_free(parsed);
	buffer_free(&blob);
	nbt_free(root);
}

TEST(nbt_full_tree_compressed_roundtrip) {
	nbt_node* root = build_full_tree();
	ASSERT(root != NULL);

	struct buffer blob = nbt_dump_compressed(root, STRAT_INFLATE);
	ASSERT(blob.data != NULL);

	nbt_node* parsed = nbt_parse_compressed(blob.data, blob.len);
	ASSERT(parsed != NULL);
	ASSERT(nbt_eq(root, parsed));

	nbt_free(parsed);
	buffer_free(&blob);
	nbt_free(root);
}

TEST(nbt_ascii_dump) {
	nbt_node* root = build_full_tree();
	ASSERT(root != NULL);

	char* ascii = nbt_dump_ascii(root);
	ASSERT(ascii != NULL);
	ASSERT(strstr(ascii, "root") != NULL);
	free(ascii);

	nbt_free(root);
}

TEST(nbt_traversal_helpers) {
	nbt_node* root = build_full_tree();
	ASSERT(root != NULL);

	// nbt_size counts root + 9 children
	ASSERT_EQ(nbt_size(root), 10U);

	// nbt_map visits every node
	int visited = 0;
	ASSERT(nbt_map(root, count_visitor, &visited));
	ASSERT_EQ(visited, 10);

	// find a node by name and by path
	nbt_node* found = nbt_find_by_name(root, "i");
	ASSERT(found != NULL && found->type == TAG_INT);
	// the root node is named "root", so paths start there
	ASSERT(nbt_find_by_path(root, "root.i") != NULL);
	ASSERT(nbt_find_by_path(root, "root.does_not_exist") == NULL);

	// filter to a copy that keeps only the int + compound nodes
	nbt_node* filtered = nbt_filter(root, keep_ints, NULL);
	ASSERT(filtered != NULL);
	ASSERT(nbt_find_by_name(filtered, "i") != NULL);
	ASSERT(nbt_find_by_name(filtered, "str") == NULL);
	nbt_free(filtered);

	nbt_free(root);
}

TEST(nbt_list_item_access) {
	// build a TAG_LIST of three ints and index into it
	nbt_node items[] = {
		{.type = TAG_INT, .name = NULL, .payload.tag_int = 11},
		{.type = TAG_INT, .name = NULL, .payload.tag_int = 22},
		{.type = TAG_INT, .name = NULL, .payload.tag_int = 33},
	};

	struct nbt_list* sentinel = malloc(sizeof(struct nbt_list));
	sentinel->data = malloc(sizeof(nbt_node));
	sentinel->data->type = TAG_INT;
	INIT_LIST_HEAD(&sentinel->entry);

	for(size_t k = 0; k < 3; k++) {
		struct nbt_list* item = malloc(sizeof(struct nbt_list));
		item->data = malloc(sizeof(nbt_node));
		*item->data = items[k];
		list_add_tail(&item->entry, &sentinel->entry);
	}

	nbt_node list = {
		.type = TAG_LIST,
		.name = dup_cstr("vals"),
		.payload.tag_list = sentinel,
	};

	ASSERT_EQ(nbt_list_item(&list, 0)->payload.tag_int, 11);
	ASSERT_EQ(nbt_list_item(&list, 2)->payload.tag_int, 33);

	// free everything but the stack-allocated `list` node itself
	nbt_free_list(sentinel);
	free(list.name);
}

TEST(nbt_long_array_roundtrip) {
	static const int64_t longs[] = {1LL << 40, -2, 99};
	int64_t* longs_heap = malloc(sizeof(longs));
	memcpy(longs_heap, longs, sizeof(longs));

	struct nbt_list* sentinel = malloc(sizeof(struct nbt_list));
	sentinel->data = NULL;
	INIT_LIST_HEAD(&sentinel->entry);

	struct nbt_list* item = malloc(sizeof(struct nbt_list));
	item->data = malloc(sizeof(nbt_node));
	*item->data = (nbt_node) {
		.type = TAG_LONG_ARRAY,
		.name = dup_cstr("la"),
		.payload.tag_long_array = {.data = longs_heap, .length = 3},
	};
	list_add_tail(&item->entry, &sentinel->entry);

	nbt_node root = {
		.type = TAG_COMPOUND,
		.name = dup_cstr("root"),
		.payload.tag_compound = sentinel,
	};

	// long arrays survive a binary dump/parse cycle (data verified directly,
	// since nbt_eq has no TAG_LONG_ARRAY case in this cNBT)
	struct buffer blob = nbt_dump_binary(&root);
	ASSERT(blob.data != NULL);

	nbt_node* parsed = nbt_parse(blob.data, blob.len);
	ASSERT(parsed != NULL);

	nbt_node* la = nbt_find_by_name(parsed, "la");
	ASSERT(la != NULL && la->type == TAG_LONG_ARRAY);
	ASSERT_EQ(la->payload.tag_long_array.length, 3);
	ASSERT_EQ(la->payload.tag_long_array.data[0], 1LL << 40);
	ASSERT_EQ(la->payload.tag_long_array.data[1], -2);
	ASSERT_EQ(la->payload.tag_long_array.data[2], 99);

	char* ascii = nbt_dump_ascii(&root);
	ASSERT(ascii != NULL);
	free(ascii);

	nbt_free(parsed);
	buffer_free(&blob);
	nbt_free_list(sentinel);
	free(root.name);
}

TEST(nbt_parse_corrupt_data) {
	unsigned char garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33};

	// not a valid zlib/gzip stream -> decompression fails gracefully
	ASSERT(nbt_parse_compressed(garbage, sizeof(garbage)) == NULL);

	// not a valid uncompressed NBT tree either
	ASSERT(nbt_parse(garbage, sizeof(garbage)) == NULL);
}

TEST(nbt_type_and_error_strings) {
	ASSERT(strcmp(nbt_type_to_string(TAG_INVALID), "TAG_END") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_BYTE), "TAG_BYTE") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_SHORT), "TAG_SHORT") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_LONG), "TAG_LONG") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_FLOAT), "TAG_FLOAT") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_DOUBLE), "TAG_DOUBLE") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_BYTE_ARRAY), "TAG_BYTE_ARRAY") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_STRING), "TAG_STRING") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_LIST), "TAG_LIST") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_COMPOUND), "TAG_COMPOUND") == 0);
	ASSERT(strcmp(nbt_type_to_string(TAG_INT_ARRAY), "TAG_INT_ARRAY") == 0);
	ASSERT(strcmp(nbt_type_to_string((nbt_type)123), "TAG_UNKNOWN") == 0);

	ASSERT(strcmp(nbt_error_to_string(NBT_ERR), "NBT tree is corrupt.") == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_EMEM),
				  "Out of memory. You should buy some RAM.")
		   == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_EZ), "Fatal zlib error. Corrupt file?")
		   == 0);
	ASSERT(strcmp(nbt_error_to_string((nbt_status)123), "Unknown error.") == 0);
}

TEST(nbt_eq_mismatches) {
	nbt_node a = {.type = TAG_INT, .name = NULL, .payload.tag_int = 1};
	nbt_node b = {.type = TAG_SHORT, .name = NULL, .payload.tag_short = 1};
	// differing types short-circuit immediately
	ASSERT(!nbt_eq(&a, &b));

	nbt_node c = {.type = TAG_INT, .name = "x", .payload.tag_int = 1};
	nbt_node d = {.type = TAG_INT, .name = "y", .payload.tag_int = 1};
	// same type/value but different names
	ASSERT(!nbt_eq(&c, &d));

	nbt_node e = {.type = TAG_INVALID, .name = NULL};
	nbt_node f = {.type = TAG_INVALID, .name = NULL};
	// invalid type hits the default branch
	ASSERT(!nbt_eq(&e, &f));

	// compounds of differing length differ
	nbt_node* root = build_full_tree();
	nbt_node* fewer = nbt_filter(root, keep_ints, NULL);
	ASSERT(fewer != NULL);
	ASSERT(!nbt_eq(root, fewer));
	nbt_free(fewer);
	nbt_free(root);
}

TEST(nbt_filter_and_inplace) {
	nbt_node* root = build_full_tree();
	ASSERT(root != NULL);

	// keep-everything filter deep-copies every scalar/array type
	nbt_node* all = nbt_filter(root, keep_all, NULL);
	ASSERT(all != NULL);
	ASSERT(nbt_eq(root, all));
	nbt_free(all);

	// nbt_find returns the first node satisfying the predicate
	nbt_node* hit = nbt_find(root, keep_ints, NULL);
	ASSERT(hit != NULL);

	// filter in place: drop the string node, keep the rest
	nbt_node* clone = nbt_clone(root);
	ASSERT(clone != NULL);
	clone = nbt_filter_inplace(clone, drop_strings, NULL);
	ASSERT(clone != NULL);
	ASSERT(nbt_find_by_name(clone, "str") == NULL);
	ASSERT(nbt_find_by_name(clone, "i") != NULL);
	nbt_free(clone);

	nbt_free(root);
}

const test_entry_t g_tests_nbt[] = {
	{"nbt_parse_level_dat", test_nbt_parse_level_dat},
	{"nbt_find_spawn_coords", test_nbt_find_spawn_coords},
	{"nbt_parse_missing_file", test_nbt_parse_missing_file},
	{"nbt_parse_truncated", test_nbt_parse_truncated},
	{"nbt_clone_roundtrip", test_nbt_clone_roundtrip},
	{"nbt_util_strings", test_nbt_util_strings},
	{"nbt_binary_roundtrip", test_nbt_binary_roundtrip},
	{"nbt_find_by_name", test_nbt_find_by_name},
	{"nbt_full_tree_binary_roundtrip", test_nbt_full_tree_binary_roundtrip},
	{"nbt_full_tree_compressed_roundtrip",
	 test_nbt_full_tree_compressed_roundtrip},
	{"nbt_ascii_dump", test_nbt_ascii_dump},
	{"nbt_traversal_helpers", test_nbt_traversal_helpers},
	{"nbt_list_item_access", test_nbt_list_item_access},
	{"nbt_long_array_roundtrip", test_nbt_long_array_roundtrip},
	{"nbt_parse_corrupt_data", test_nbt_parse_corrupt_data},
	{"nbt_type_and_error_strings", test_nbt_type_and_error_strings},
	{"nbt_eq_mismatches", test_nbt_eq_mismatches},
	{"nbt_filter_and_inplace", test_nbt_filter_and_inplace},
};

const size_t g_tests_nbt_count
	= sizeof(g_tests_nbt) / sizeof(g_tests_nbt[0]);
