file(GLOB SOURCE_CODE
    "src/*.cc"
)


idf_component_register(
    SRCS
        ${SOURCE_CODE}
    INCLUDE_DIRS
        "include"
)

# 禁用 C++ 异常（本组件要求 -fno-exceptions 环境）
target_compile_options(${COMPONENT_LIB} INTERFACE -fno-exceptions)

# 要求 C++17（用于 std::launder 等特性）
target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)