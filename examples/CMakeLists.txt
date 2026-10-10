cmake_minimum_required(VERSION 3.16)
project(my_linklist_tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 从 tests/ 往上推导
get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 测试可执行文件
add_executable(test_my_linklist
    ${CMAKE_CURRENT_SOURCE_DIR}/test_my_linklist.cc
    ${ROOT_DIR}/src/my_list_base.cc
)

target_include_directories(test_my_linklist PRIVATE
    ${ROOT_DIR}/include
    ${ROOT_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}          # ← 关键：从本地找 test_helpers.h
)

target_compile_options(test_my_linklist PRIVATE
    -Wall -Wextra
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -g -O1
)
target_link_options(test_my_linklist PRIVATE
    -fsanitize=address,undefined
)

enable_testing()
add_test(NAME my_linklist COMMAND test_my_linklist)