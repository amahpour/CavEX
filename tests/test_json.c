#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "parson.h"

static const char* DOC =
	"{"
	"  \"name\": \"Claude World\","
	"  \"seed\": 1234567890,"
	"  \"hardcore\": true,"
	"  \"motd\": null,"
	"  \"spawn\": {\"x\": 24, \"y\": 64, \"z\": -8},"
	"  \"nums\": [10, 20, 30],"
	"  \"mixed\": [\"a\", 2, false, null, {\"k\": 1}],"
	"  \"ratio\": 0.5,"
	"  \"escaped\": \"a\\tb\\nc\\\"d\""
	"}";

TEST(json_parse_and_read) {
	JSON_Value* root = json_parse_string(DOC);
	ASSERT(root != NULL);
	ASSERT_EQ(json_value_get_type(root), JSONObject);
	ASSERT_EQ(json_type(root), JSONObject);

	JSON_Object* obj = json_value_get_object(root);
	ASSERT(obj != NULL);

	ASSERT(strcmp(json_object_get_string(obj, "name"), "Claude World") == 0);
	ASSERT_EQ(json_object_get_string_len(obj, "name"), strlen("Claude World"));
	ASSERT_NEAR(json_object_get_number(obj, "seed"), 1234567890.0, 1.0);
	ASSERT_EQ(json_object_get_boolean(obj, "hardcore"), 1);
	ASSERT_NEAR(json_object_get_number(obj, "ratio"), 0.5, 0.0001);

	// missing keys return sentinel values
	ASSERT(json_object_get_string(obj, "absent") == NULL);
	ASSERT_NEAR(json_object_get_number(obj, "absent"), 0.0, 0.0001);
	ASSERT_EQ(json_object_get_boolean(obj, "absent"), -1);

	// nested object + dotget
	JSON_Object* spawn = json_object_get_object(obj, "spawn");
	ASSERT(spawn != NULL);
	ASSERT_NEAR(json_object_get_number(spawn, "y"), 64.0, 0.0001);
	ASSERT_NEAR(json_object_dotget_number(obj, "spawn.x"), 24.0, 0.0001);
	ASSERT(json_object_dotget_value(obj, "spawn.z") != NULL);
	ASSERT(json_object_dotget_object(obj, "spawn") != NULL);

	// arrays
	JSON_Array* nums = json_object_get_array(obj, "nums");
	ASSERT(nums != NULL);
	ASSERT_EQ(json_array_get_count(nums), 3U);
	ASSERT_NEAR(json_array_get_number(nums, 1), 20.0, 0.0001);
	ASSERT(json_array_get_wrapping_value(nums) != NULL);

	JSON_Array* mixed = json_object_get_array(obj, "mixed");
	ASSERT_EQ(json_array_get_count(mixed), 5U);
	ASSERT(strcmp(json_array_get_string(mixed, 0), "a") == 0);
	ASSERT_EQ(json_array_get_string_len(mixed, 0), 1U);
	ASSERT_NEAR(json_array_get_number(mixed, 1), 2.0, 0.0001);
	ASSERT_EQ(json_array_get_boolean(mixed, 2), 0);
	ASSERT_EQ(json_value_get_type(json_array_get_value(mixed, 3)), JSONNull);
	ASSERT(json_array_get_object(mixed, 4) != NULL);

	// null value
	ASSERT_EQ(json_value_get_type(json_object_get_value(obj, "motd")), JSONNull);

	json_value_free(root);
}

TEST(json_has_and_count) {
	JSON_Value* root = json_parse_string(DOC);
	ASSERT(root != NULL);
	JSON_Object* obj = json_value_get_object(root);

	ASSERT_EQ(json_object_has_value(obj, "name"), 1);
	ASSERT_EQ(json_object_has_value(obj, "absent"), 0);
	ASSERT_EQ(json_object_has_value_of_type(obj, "seed", JSONNumber), 1);
	ASSERT_EQ(json_object_has_value_of_type(obj, "seed", JSONString), 0);
	ASSERT_EQ(json_object_dothas_value(obj, "spawn.x"), 1);
	ASSERT_EQ(json_object_dothas_value_of_type(obj, "spawn.x", JSONNumber), 1);

	size_t count = json_object_get_count(obj);
	ASSERT(count >= 8U);
	ASSERT(json_object_get_name(obj, 0) != NULL);
	ASSERT(json_object_get_value_at(obj, 0) != NULL);
	ASSERT(json_object_get_wrapping_value(obj) == root);

	json_value_free(root);
}

TEST(json_build_and_serialize) {
	JSON_Value* root = json_value_init_object();
	JSON_Object* obj = json_value_get_object(root);
	ASSERT(root != NULL && obj != NULL);

	ASSERT_EQ(json_object_set_string(obj, "title", "hello"), JSONSuccess);
	ASSERT_EQ(json_object_set_number(obj, "count", 3), JSONSuccess);
	ASSERT_EQ(json_object_set_boolean(obj, "flag", 1), JSONSuccess);
	ASSERT_EQ(json_object_set_null(obj, "nothing"), JSONSuccess);
	ASSERT_EQ(json_object_dotset_number(obj, "nested.deep.value", 7),
			  JSONSuccess);
	ASSERT_EQ(json_object_dotset_string(obj, "nested.deep.name", "x"),
			  JSONSuccess);
	ASSERT_EQ(json_object_dotset_boolean(obj, "nested.on", 1), JSONSuccess);
	ASSERT_EQ(json_object_dotset_null(obj, "nested.none"), JSONSuccess);

	// an array built up element by element
	JSON_Value* arrv = json_value_init_array();
	JSON_Array* arr = json_value_get_array(arrv);
	ASSERT_EQ(json_array_append_number(arr, 1), JSONSuccess);
	ASSERT_EQ(json_array_append_string(arr, "two"), JSONSuccess);
	ASSERT_EQ(json_array_append_boolean(arr, 0), JSONSuccess);
	ASSERT_EQ(json_array_append_null(arr), JSONSuccess);
	JSON_Value* childv = json_value_init_object();
	ASSERT_EQ(json_object_set_number(json_value_get_object(childv), "z", 9),
			  JSONSuccess);
	ASSERT_EQ(json_array_append_value(arr, childv), JSONSuccess);
	ASSERT_EQ(json_object_set_value(obj, "list", arrv), JSONSuccess);

	ASSERT_NEAR(json_object_dotget_number(obj, "nested.deep.value"), 7.0,
				0.0001);

	// compact + pretty serialization
	char* compact = json_serialize_to_string(root);
	ASSERT(compact != NULL);
	ASSERT(strstr(compact, "title") != NULL);
	json_free_serialized_string(compact);

	size_t sz = json_serialization_size(root);
	ASSERT(sz > 0);
	char* buf = malloc(sz);
	ASSERT(buf != NULL);
	ASSERT_EQ(json_serialize_to_buffer(root, buf, sz), JSONSuccess);
	free(buf);

	char* pretty = json_serialize_to_string_pretty(root);
	ASSERT(pretty != NULL);
	ASSERT(json_serialization_size_pretty(root) > 0);

	// round-trip the pretty form back through the parser
	JSON_Value* reparsed = json_parse_string(pretty);
	ASSERT(reparsed != NULL);
	ASSERT(json_value_equals(root, reparsed));
	json_value_free(reparsed);
	json_free_serialized_string(pretty);

	json_value_free(root);
}

TEST(json_modify_and_remove) {
	JSON_Value* root = json_parse_string(DOC);
	JSON_Object* obj = json_value_get_object(root);
	ASSERT(root != NULL);

	// replace an array element, then remove one
	JSON_Array* nums = json_object_get_array(obj, "nums");
	ASSERT_EQ(json_array_replace_number(nums, 0, 99), JSONSuccess);
	ASSERT_NEAR(json_array_get_number(nums, 0), 99.0, 0.0001);
	ASSERT_EQ(json_array_replace_string(nums, 1, "s"), JSONSuccess);
	ASSERT_EQ(json_array_replace_boolean(nums, 2, 1), JSONSuccess);
	ASSERT_EQ(json_array_remove(nums, 0), JSONSuccess);
	ASSERT_EQ(json_array_get_count(nums), 2U);
	ASSERT_EQ(json_array_remove(nums, 100), JSONFailure);

	// remove keys
	ASSERT_EQ(json_object_remove(obj, "ratio"), JSONSuccess);
	ASSERT_EQ(json_object_has_value(obj, "ratio"), 0);
	ASSERT_EQ(json_object_dotremove(obj, "spawn.x"), JSONSuccess);
	ASSERT_EQ(json_object_dothas_value(obj, "spawn.x"), 0);

	// overwrite an existing key with a new type
	ASSERT_EQ(json_object_set_number(obj, "name", 5), JSONSuccess);
	ASSERT_EQ(json_value_get_type(json_object_get_value(obj, "name")),
			  JSONNumber);

	json_value_free(root);
}

TEST(json_deepcopy_validate_clear) {
	JSON_Value* root = json_parse_string(DOC);
	ASSERT(root != NULL);

	JSON_Value* copy = json_value_deep_copy(root);
	ASSERT(copy != NULL);
	ASSERT(json_value_equals(root, copy));

	// schema validation: a subset schema validates the document
	JSON_Value* schema = json_parse_string(
		"{\"name\":\"\",\"seed\":0,\"spawn\":{\"x\":0}}");
	ASSERT(schema != NULL);
	ASSERT_EQ(json_validate(schema, root), JSONSuccess);

	JSON_Value* bad_schema = json_parse_string("{\"missing\":0}");
	ASSERT_EQ(json_validate(bad_schema, root), JSONFailure);
	json_value_free(bad_schema);
	json_value_free(schema);

	// clear empties an object
	JSON_Object* cobj = json_value_get_object(copy);
	ASSERT_EQ(json_object_clear(cobj), JSONSuccess);
	ASSERT_EQ(json_object_get_count(cobj), 0U);

	json_value_free(copy);
	json_value_free(root);
}

TEST(json_scalar_values_and_parents) {
	JSON_Value* s = json_value_init_string("hi");
	JSON_Value* n = json_value_init_number(3.5);
	JSON_Value* b = json_value_init_boolean(1);
	JSON_Value* z = json_value_init_null();

	ASSERT(strcmp(json_value_get_string(s), "hi") == 0);
	ASSERT_EQ(json_value_get_string_len(s), 2U);
	ASSERT(strcmp(json_string(s), "hi") == 0);
	ASSERT_EQ(json_string_len(s), 2U);
	ASSERT_NEAR(json_value_get_number(n), 3.5, 0.0001);
	ASSERT_NEAR(json_number(n), 3.5, 0.0001);
	ASSERT_EQ(json_value_get_boolean(b), 1);
	ASSERT_EQ(json_boolean(b), 1);
	ASSERT_EQ(json_value_get_type(z), JSONNull);

	// a child value reports its parent
	JSON_Value* root = json_value_init_object();
	json_object_set_value(json_value_get_object(root), "child", s);
	ASSERT_EQ(json_value_get_parent(s), root);

	json_value_free(root); // frees s too
	json_value_free(n);
	json_value_free(b);
	json_value_free(z);
}

TEST(json_parse_failures_and_comments) {
	ASSERT(json_parse_string("{ not valid json ") == NULL);
	ASSERT(json_parse_string("") == NULL);
	ASSERT(json_parse_string("@@@") == NULL);
	ASSERT(json_parse_string("{\"a\": }") == NULL);
	ASSERT(json_parse_file("/tmp/cavex_missing_json_file_xyz.json") == NULL);

	JSON_Value* commented = json_parse_string_with_comments(
		"{ /* c */ \"a\": 1, // line\n \"b\": 2 }");
	ASSERT(commented != NULL);
	ASSERT_NEAR(json_object_get_number(json_value_get_object(commented), "b"),
				2.0, 0.0001);
	json_value_free(commented);
}

TEST(json_file_roundtrip) {
	char tmpl[] = "/tmp/cavex_json_XXXXXX";
	int fd = mkstemp(tmpl);
	ASSERT(fd >= 0);
	close(fd);

	JSON_Value* root = json_value_init_object();
	json_object_set_string(json_value_get_object(root), "k", "v");
	ASSERT_EQ(json_serialize_to_file(root, tmpl), JSONSuccess);
	ASSERT_EQ(json_serialize_to_file_pretty(root, tmpl), JSONSuccess);

	JSON_Value* loaded = json_parse_file(tmpl);
	ASSERT(loaded != NULL);
	ASSERT(strcmp(json_object_get_string(json_value_get_object(loaded), "k"),
				  "v")
		   == 0);

	json_value_free(loaded);
	json_value_free(root);
	remove(tmpl);
}

TEST(json_unicode_strings) {
	JSON_Value* root = json_parse_string(
		"{\"e\":\"caf\\u00e9\",\"emoji\":\"\\uD83D\\uDE00\","
		"\"nested\":{\"s\":\"hi\",\"flag\":true}}");
	ASSERT(root != NULL);
	JSON_Object* obj = json_value_get_object(root);

	// \u escapes (incl. a UTF-16 surrogate pair) decode to UTF-8
	ASSERT(json_object_get_string(obj, "e") != NULL);
	ASSERT(json_object_get_string(obj, "emoji") != NULL);

	// dotget string + length + boolean accessors
	ASSERT(strcmp(json_object_dotget_string(obj, "nested.s"), "hi") == 0);
	ASSERT_EQ(json_object_dotget_string_len(obj, "nested.s"), 2U);
	ASSERT_EQ(json_object_dotget_boolean(obj, "nested.flag"), 1);

	// serialize back out (escapes the non-ASCII bytes again)
	char* s = json_serialize_to_string(root);
	ASSERT(s != NULL);
	json_free_serialized_string(s);

	json_value_free(root);
}

TEST(json_with_len_and_nested_arrays) {
	JSON_Value* root = json_value_init_object();
	JSON_Object* obj = json_value_get_object(root);

	// *_with_len truncates to the requested length
	ASSERT_EQ(json_object_set_string_with_len(obj, "a", "hello", 3),
			  JSONSuccess);
	ASSERT(strcmp(json_object_get_string(obj, "a"), "hel") == 0);
	ASSERT_EQ(json_object_dotset_string_with_len(obj, "deep.b", "world", 2),
			  JSONSuccess);
	ASSERT(strcmp(json_object_dotget_string(obj, "deep.b"), "wo") == 0);

	// an array holding another array; json_array() shortcut accessor
	JSON_Value* arrv = json_value_init_array();
	JSON_Array* arr = json_array(arrv);
	ASSERT(arr != NULL);

	JSON_Value* innerv = json_value_init_array();
	ASSERT_EQ(json_array_append_number(json_array(innerv), 1), JSONSuccess);
	ASSERT_EQ(json_array_append_value(arr, innerv), JSONSuccess);
	ASSERT(json_array_get_array(arr, 0) != NULL);

	ASSERT_EQ(json_array_append_string_with_len(arr, "abcdef", 3), JSONSuccess);
	ASSERT(strcmp(json_array_get_string(arr, 1), "abc") == 0);
	ASSERT_EQ(json_array_append_null(arr), JSONSuccess);

	ASSERT_EQ(json_array_replace_string_with_len(arr, 1, "xyz123", 2),
			  JSONSuccess);
	ASSERT(strcmp(json_array_get_string(arr, 1), "xy") == 0);
	ASSERT_EQ(json_array_replace_null(arr, 1), JSONSuccess);
	ASSERT_EQ(json_value_get_type(json_array_get_value(arr, 1)), JSONNull);

	ASSERT_EQ(json_array_clear(arr), JSONSuccess);
	ASSERT_EQ(json_array_get_count(arr), 0U);
	ASSERT_EQ(json_object_set_value(obj, "list", arrv), JSONSuccess);

	// value-level string-with-len constructor
	JSON_Value* sv = json_value_init_string_with_len("truncated", 4);
	ASSERT(strcmp(json_value_get_string(sv), "trun") == 0);
	json_value_free(sv);

	json_value_free(root);
}

static int num_serializer(double num, char* buf) {
	if(!buf)
		return 16; // worst-case length
	return sprintf(buf, "%g", num);
}

TEST(json_global_settings) {
	JSON_Value* v = json_value_init_object();
	JSON_Object* obj = json_value_get_object(v);

	// slashes left unescaped when disabled
	json_set_escape_slashes(0);
	ASSERT_EQ(json_object_set_string(obj, "url", "a/b/c"), JSONSuccess);
	char* s = json_serialize_to_string(v);
	ASSERT(s != NULL && strstr(s, "a/b/c") != NULL);
	json_free_serialized_string(s);
	json_set_escape_slashes(1); // restore default

	// custom float format
	json_set_float_serialization_format("%.2f");
	ASSERT_EQ(json_object_set_number(obj, "n", 3.14159), JSONSuccess);
	s = json_serialize_to_string(v);
	ASSERT(s != NULL);
	json_free_serialized_string(s);
	json_set_float_serialization_format(NULL); // restore default

	// custom number serialization function
	json_set_number_serialization_function(num_serializer);
	s = json_serialize_to_string(v);
	ASSERT(s != NULL);
	json_free_serialized_string(s);
	json_set_number_serialization_function(NULL); // restore default

	// explicitly install the stdlib allocator (parson's default)
	json_set_allocation_functions(malloc, free);

	json_value_free(v);

	// parse a file that contains comments
	char tmpl[] = "/tmp/cavex_jsonc_XXXXXX";
	int fd = mkstemp(tmpl);
	ASSERT(fd >= 0);
	const char* doc = "{ /* block */ \"a\": 1 } // line\n";
	ASSERT(write(fd, doc, strlen(doc)) == (ssize_t)strlen(doc));
	close(fd);

	JSON_Value* loaded = json_parse_file_with_comments(tmpl);
	ASSERT(loaded != NULL);
	ASSERT_NEAR(json_object_get_number(json_value_get_object(loaded), "a"), 1.0,
				0.0001);
	json_value_free(loaded);
	remove(tmpl);
}

TEST(json_null_argument_guards) {
	// object accessors tolerate a NULL object and return sentinels
	ASSERT(json_object_get_value(NULL, "x") == NULL);
	ASSERT(json_object_get_string(NULL, "x") == NULL);
	ASSERT_EQ(json_object_get_string_len(NULL, "x"), 0U);
	ASSERT(json_object_get_object(NULL, "x") == NULL);
	ASSERT(json_object_get_array(NULL, "x") == NULL);
	ASSERT_NEAR(json_object_get_number(NULL, "x"), 0.0, 0.0001);
	ASSERT_EQ(json_object_get_boolean(NULL, "x"), -1);
	ASSERT(json_object_dotget_value(NULL, "x") == NULL);
	ASSERT(json_object_dotget_string(NULL, "x") == NULL);
	ASSERT(json_object_dotget_object(NULL, "x") == NULL);
	ASSERT(json_object_dotget_array(NULL, "x") == NULL);
	ASSERT_NEAR(json_object_dotget_number(NULL, "x"), 0.0, 0.0001);
	ASSERT_EQ(json_object_dotget_boolean(NULL, "x"), -1);
	ASSERT_EQ(json_object_get_count(NULL), 0U);
	ASSERT(json_object_get_name(NULL, 0) == NULL);
	ASSERT(json_object_get_value_at(NULL, 0) == NULL);
	ASSERT(json_object_get_wrapping_value(NULL) == NULL);
	ASSERT_EQ(json_object_has_value(NULL, "x"), 0);
	ASSERT_EQ(json_object_has_value_of_type(NULL, "x", JSONString), 0);
	ASSERT_EQ(json_object_dothas_value(NULL, "x"), 0);
	ASSERT_EQ(json_object_set_value(NULL, "x", NULL), JSONFailure);
	ASSERT_EQ(json_object_set_string(NULL, "x", "y"), JSONFailure);
	ASSERT_EQ(json_object_set_number(NULL, "x", 1), JSONFailure);
	ASSERT_EQ(json_object_set_boolean(NULL, "x", 1), JSONFailure);
	ASSERT_EQ(json_object_set_null(NULL, "x"), JSONFailure);
	ASSERT_EQ(json_object_dotset_string(NULL, "x", "y"), JSONFailure);
	ASSERT_EQ(json_object_dotset_number(NULL, "x", 1), JSONFailure);
	ASSERT_EQ(json_object_dotset_boolean(NULL, "x", 1), JSONFailure);
	ASSERT_EQ(json_object_dotset_null(NULL, "x"), JSONFailure);
	ASSERT_EQ(json_object_remove(NULL, "x"), JSONFailure);
	ASSERT_EQ(json_object_dotremove(NULL, "x"), JSONFailure);
	ASSERT_EQ(json_object_clear(NULL), JSONFailure);

	// array accessors tolerate a NULL array
	ASSERT(json_array_get_value(NULL, 0) == NULL);
	ASSERT(json_array_get_string(NULL, 0) == NULL);
	ASSERT(json_array_get_object(NULL, 0) == NULL);
	ASSERT(json_array_get_array(NULL, 0) == NULL);
	ASSERT_NEAR(json_array_get_number(NULL, 0), 0.0, 0.0001);
	ASSERT_EQ(json_array_get_boolean(NULL, 0), -1);
	ASSERT_EQ(json_array_get_count(NULL), 0U);
	ASSERT(json_array_get_wrapping_value(NULL) == NULL);
	ASSERT_EQ(json_array_append_value(NULL, NULL), JSONFailure);
	ASSERT_EQ(json_array_append_string(NULL, "x"), JSONFailure);
	ASSERT_EQ(json_array_append_number(NULL, 1), JSONFailure);
	ASSERT_EQ(json_array_append_boolean(NULL, 1), JSONFailure);
	ASSERT_EQ(json_array_append_null(NULL), JSONFailure);
	ASSERT_EQ(json_array_replace_value(NULL, 0, NULL), JSONFailure);
	ASSERT_EQ(json_array_replace_string(NULL, 0, "x"), JSONFailure);
	ASSERT_EQ(json_array_replace_number(NULL, 0, 1), JSONFailure);
	ASSERT_EQ(json_array_replace_boolean(NULL, 0, 1), JSONFailure);
	ASSERT_EQ(json_array_replace_null(NULL, 0), JSONFailure);
	ASSERT_EQ(json_array_remove(NULL, 0), JSONFailure);
	ASSERT_EQ(json_array_clear(NULL), JSONFailure);

	// value accessors tolerate a NULL value
	ASSERT_EQ(json_value_get_type(NULL), JSONError);
	ASSERT(json_value_get_object(NULL) == NULL);
	ASSERT(json_value_get_array(NULL) == NULL);
	ASSERT(json_value_get_string(NULL) == NULL);
	ASSERT_EQ(json_value_get_string_len(NULL), 0U);
	ASSERT_NEAR(json_value_get_number(NULL), 0.0, 0.0001);
	ASSERT_EQ(json_value_get_boolean(NULL), -1);
	ASSERT(json_value_get_parent(NULL) == NULL);
	ASSERT(json_value_deep_copy(NULL) == NULL);
	ASSERT_EQ(json_serialization_size(NULL), 0U);
	ASSERT(json_serialize_to_string(NULL) == NULL);
	ASSERT(json_value_init_string(NULL) == NULL);
	json_value_free(NULL); // no-op on NULL

	// type-mismatched accessors return sentinels too
	JSON_Value* num = json_value_init_number(5);
	ASSERT(json_value_get_object(num) == NULL);
	ASSERT(json_value_get_array(num) == NULL);
	ASSERT(json_value_get_string(num) == NULL);
	json_value_free(num);
}

TEST(json_raw_utf8_and_escapes) {
	// raw 2-, 3- and 4-byte UTF-8 sequences in the input
	JSON_Value* u = json_parse_string(
		"{\"s\":\"\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"}");
	ASSERT(u != NULL);
	const char* s = json_object_get_string(json_value_get_object(u), "s");
	ASSERT(s != NULL);
	// serialize it back out (re-encodes the multi-byte sequences)
	char* out = json_serialize_to_string(u);
	ASSERT(out != NULL);
	json_free_serialized_string(out);
	json_value_free(u);

	// every backslash escape on the input side
	JSON_Value* e = json_parse_string("{\"e\":\"\\b\\f\\n\\r\\t\\/\\\\\\\"\"}");
	ASSERT(e != NULL);
	const char* es = json_object_get_string(json_value_get_object(e), "e");
	ASSERT(es != NULL);
	ASSERT(es[0] == '\b');
	// and on the output side (control characters force \uXXXX / \b... escapes)
	char* eout = json_serialize_to_string(e);
	ASSERT(eout != NULL);
	json_free_serialized_string(eout);
	json_value_free(e);
}

TEST(json_parse_corners) {
	// partial / nested parse failures exercise the mid-parse cleanup paths
	ASSERT(json_parse_string("{\"a\":}") == NULL);
	ASSERT(json_parse_string("{\"a\":[1 2]}") == NULL);
	ASSERT(json_parse_string("[1,2,@]") == NULL);
	ASSERT(json_parse_string("{\"a\":@}") == NULL);
	ASSERT(json_parse_string("{\"a\":1 \"b\":2}") == NULL);
	ASSERT(json_parse_string("{\"a\" 1}") == NULL);

	// a UTF-8 BOM prefix is skipped before the value
	JSON_Value* bom = json_parse_string("\xEF\xBB\xBF{\"a\":1}");
	ASSERT(bom != NULL);
	json_value_free(bom);

	// a 3-byte BMP codepoint via \u escape (euro sign) round-trips
	JSON_Value* euro = json_parse_string("{\"c\":\"\\u20AC\"}");
	ASSERT(euro != NULL);
	char* es = json_serialize_to_string(euro);
	ASSERT(es != NULL);
	json_free_serialized_string(es);
	json_value_free(euro);

	// serializing into a far-too-small buffer fails cleanly
	JSON_Value* big = json_parse_string("{\"a\":1,\"b\":2,\"c\":3}");
	ASSERT(big != NULL);
	char tiny[2];
	ASSERT_EQ(json_serialize_to_buffer(big, tiny, sizeof(tiny)), JSONFailure);
	json_value_free(big);

	// validation against array and nested-object schemas
	JSON_Value* schema = json_parse_string("{\"list\":[0],\"o\":{\"k\":\"\"}}");
	JSON_Value* doc = json_parse_string("{\"list\":[1,2,3],\"o\":{\"k\":\"v\"}}");
	ASSERT(schema != NULL && doc != NULL);
	ASSERT_EQ(json_validate(schema, doc), JSONSuccess);
	json_value_free(schema);
	json_value_free(doc);
}

const test_entry_t g_tests_json[] = {
	{"json_parse_and_read", test_json_parse_and_read},
	{"json_has_and_count", test_json_has_and_count},
	{"json_build_and_serialize", test_json_build_and_serialize},
	{"json_modify_and_remove", test_json_modify_and_remove},
	{"json_deepcopy_validate_clear", test_json_deepcopy_validate_clear},
	{"json_scalar_values_and_parents", test_json_scalar_values_and_parents},
	{"json_parse_failures_and_comments", test_json_parse_failures_and_comments},
	{"json_file_roundtrip", test_json_file_roundtrip},
	{"json_unicode_strings", test_json_unicode_strings},
	{"json_with_len_and_nested_arrays", test_json_with_len_and_nested_arrays},
	{"json_global_settings", test_json_global_settings},
	{"json_null_argument_guards", test_json_null_argument_guards},
	{"json_raw_utf8_and_escapes", test_json_raw_utf8_and_escapes},
	{"json_parse_corners", test_json_parse_corners},
};

const size_t g_tests_json_count = sizeof(g_tests_json) / sizeof(g_tests_json[0]);
