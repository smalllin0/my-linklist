cmake_minimum_required(VERSION 3.16)
project(Host_tests)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET Host_tests)

get_filename_component(ROOT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 通配收集源文件（CONFIGURE_DEPENDS 让新增文件能触发重新配置）
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
file(GLOB LIB_SOURCES CONFIGURE_DEPENDS
    ${ROOT_DIR}/src/*.c
    ${ROOT_DIR}/src/*.cc
    ${ROOT_DIR}/src/*.cpp
)

add_executable(${TARGET}
    ${TEST_SOURCES}
    ${LIB_SOURCES}
)

target_include_directories(${TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ROOT_DIR}/include
        ${ROOT_DIR}/src
)

target_compile_options(${TARGET} PRIVATE
    -Wall -Wextra
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -g -O1
)
target_link_options(${TARGET} PRIVATE
    -fsanitize=address,undefined
)

enable_testing()
add_test(NAME ${TARGET} COMMAND ${TARGET})