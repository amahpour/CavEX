find_package(ZLIB REQUIRED)

add_library(cavex_testlib STATIC
	${CMAKE_SOURCE_DIR}/source/util.c
	${CMAKE_SOURCE_DIR}/source/stack.c
	${CMAKE_SOURCE_DIR}/source/block/aabb.c
	${CMAKE_SOURCE_DIR}/source/block/blocks_util.c
	${CMAKE_SOURCE_DIR}/source/block/block_bubble_column.c
	${CMAKE_SOURCE_DIR}/source/block/block_candle.c
	${CMAKE_SOURCE_DIR}/source/block/face_occlusion.c
	${CMAKE_SOURCE_DIR}/source/cNBT/buffer.c
	${CMAKE_SOURCE_DIR}/source/cNBT/nbt_parsing.c
	${CMAKE_SOURCE_DIR}/source/cNBT/nbt_loading.c
	${CMAKE_SOURCE_DIR}/source/cNBT/nbt_treeops.c
	${CMAKE_SOURCE_DIR}/source/cNBT/nbt_util.c
	${CMAKE_SOURCE_DIR}/source/daytime.c
	${CMAKE_SOURCE_DIR}/source/config.c
	${CMAKE_SOURCE_DIR}/source/parson/parson.c
	${CMAKE_SOURCE_DIR}/source/lighting.c
	${CMAKE_SOURCE_DIR}/source/item/recipe.c
	${CMAKE_SOURCE_DIR}/source/item/tool.c
	${CMAKE_SOURCE_DIR}/source/item/inventory.c
	${CMAKE_SOURCE_DIR}/source/item/window_container.c
	${CMAKE_SOURCE_DIR}/source/network/inventory_logic.c
	${CMAKE_SOURCE_DIR}/source/network/level_archive.c
	${CMAKE_SOURCE_DIR}/source/network/region_archive.c
	${CMAKE_SOURCE_DIR}/source/chunk_storage.c
	${CMAKE_SOURCE_DIR}/source/entity/entity_id.c
	${CMAKE_SOURCE_DIR}/source/entity/entity_local_player.c
	stubs/chunk_link_stub.c
	stubs/game_state_stub.c
	stubs/blocks_stub.c
	stubs/items_stub.c
	stubs/render_stub.c
	stubs/entity_player_stub.c
)

target_include_directories(cavex_testlib PRIVATE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/source
	${CMAKE_SOURCE_DIR}/source/cNBT
	${CMAKE_SOURCE_DIR}/source/block
	${CMAKE_SOURCE_DIR}/source/parson
	${CMAKE_SOURCE_DIR}/source/item
	${CMAKE_SOURCE_DIR}/source/network
	${CMAKE_SOURCE_DIR}/source/entity
)

target_compile_definitions(cavex_testlib PRIVATE PLATFORM_PC CGLM_ALL_UNALIGNED)
set_target_properties(cavex_testlib PROPERTIES C_STANDARD 99)

if(CAVEX_COVERAGE)
	target_compile_options(cavex_testlib PRIVATE --coverage -O0)
	target_link_options(cavex_testlib PRIVATE --coverage)
endif()

add_executable(cavex_tests
	test_main.c
	test_path.c
	test_stack.c
	test_util.c
	test_aabb.c
	test_blocks.c
	test_face_occlusion.c
	test_buffer.c
	test_daytime.c
	test_config.c
	test_json.c
	test_nbt.c
	test_lighting.c
	test_recipe.c
	test_tool.c
	test_entity.c
	test_double_tap.c
	test_chunk.c
	test_inventory.c
	test_window.c
	test_level_archive.c
	test_region.c
)

target_include_directories(cavex_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/source
	${CMAKE_SOURCE_DIR}/source/cNBT
	${CMAKE_SOURCE_DIR}/source/block
	${CMAKE_SOURCE_DIR}/source/parson
	${CMAKE_SOURCE_DIR}/source/item
	${CMAKE_SOURCE_DIR}/source/network
	${CMAKE_SOURCE_DIR}/source/entity
)
target_compile_definitions(cavex_tests PRIVATE
	PLATFORM_PC
	CGLM_ALL_UNALIGNED
	CAVEX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(cavex_tests PRIVATE cavex_testlib ZLIB::ZLIB m)
set_target_properties(cavex_tests PROPERTIES C_STANDARD 99)

if(CAVEX_COVERAGE)
	target_link_options(cavex_tests PRIVATE --coverage)
endif()

add_test(NAME cavex_tests COMMAND cavex_tests)
