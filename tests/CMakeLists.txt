cmake_minimum_required(VERSION 3.16)
project(my_linklist_tests)

set(CMAKE_CXX_STANDARD  23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_my_linklist
    test_my_linklist.cc
    ../src/my_list_base.cc
)
target_include_directories(test_my_linklist PRIVATE
    ../include
    ../src
    ../../../my-test-utils/include         # 引用共享工具
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