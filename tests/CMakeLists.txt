add_library(cavex_testlib STATIC
	${CMAKE_SOURCE_DIR}/source/util.c
	${CMAKE_SOURCE_DIR}/source/stack.c
	${CMAKE_SOURCE_DIR}/source/block/aabb.c
	${CMAKE_SOURCE_DIR}/source/block/face_occlusion.c
	${CMAKE_SOURCE_DIR}/source/cNBT/buffer.c
)

target_include_directories(cavex_testlib PRIVATE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/source
	${CMAKE_SOURCE_DIR}/source/cNBT
	${CMAKE_SOURCE_DIR}/source/block
)

target_compile_definitions(cavex_testlib PRIVATE PLATFORM_PC CGLM_ALL_UNALIGNED)
set_target_properties(cavex_testlib PROPERTIES C_STANDARD 99)

if(CAVEX_COVERAGE)
	target_compile_options(cavex_testlib PRIVATE --coverage -O0)
	target_link_options(cavex_testlib PRIVATE --coverage)
endif()

add_executable(cavex_tests
	test_main.c
	test_stack.c
	test_util.c
	test_aabb.c
	test_face_occlusion.c
	test_buffer.c
)

target_include_directories(cavex_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/source
	${CMAKE_SOURCE_DIR}/source/cNBT
	${CMAKE_SOURCE_DIR}/source/block
)
target_compile_definitions(cavex_tests PRIVATE PLATFORM_PC CGLM_ALL_UNALIGNED)
target_link_libraries(cavex_tests PRIVATE cavex_testlib m)
set_target_properties(cavex_tests PROPERTIES C_STANDARD 99)

if(CAVEX_COVERAGE)
	target_link_options(cavex_tests PRIVATE --coverage)
endif()

add_test(NAME cavex_tests COMMAND cavex_tests)
