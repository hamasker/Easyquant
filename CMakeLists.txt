# /***************************
# *
# *
# *
# *
# ***************************
cmake_minimum_required(VERSION 3.10...3.28)
message(STATUS "Running cmake version ${CMAKE_VERSION}")

# 检查平台架构，并设置 AVX（仅限 x86_64）
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(STATUS "x86_64 detected, enabling AVX")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
else()
    message(STATUS "Non-x86_64 architecture (${CMAKE_SYSTEM_PROCESSOR}), skipping AVX")
endif()

# 设置编译器（可以按需替换 gcc 为 clang）
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

# # 抑制 unused parameter 警告（主要来自外部库）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# 项目名称
project(Relaxquant)

set(CMAKE_CXX_STANDARD 20) # 升级到C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 强制要求该标准
set(CMAKE_CXX_EXTENSIONS OFF) # 不用gcc扩展，保证兼容性

# 添加项目根目录的 include 目录到全局包含路径
include_directories(${PROJECT_SOURCE_DIR}/include)

# 以子目录方式引入 NovaBase（避免相对路径 add_subdirectory 解析到顶层 src）
add_subdirectory(ext/NovaBase)

# 读取版本号文件 VERSION.txt
file(STRINGS "VERSION.txt" NOVA_VERSION)
string(REPLACE "." ";" NOVA_VERSION_LIST ${NOVA_VERSION})
list(GET NOVA_VERSION_LIST 0 NOVA_VERSION_MAJOR)
list(GET NOVA_VERSION_LIST 1 NOVA_VERSION_MINOR)
list(GET NOVA_VERSION_LIST 2 NOVA_VERSION_PATCH1)
list(GET NOVA_VERSION_LIST 3 NOVA_VERSION_PATCH2)

# 设置 ext 模块的 include 和 lib 路径
NOVA_GET_SUBDIRS(${PROJECT_SOURCE_DIR}/ext submodule_dirs)

foreach(submodule_dir ${submodule_dirs})
    if(EXISTS ${submodule_dir}/include)
        NOVA_SET_COMMON_DIRECTORIES(${submodule_dir})
    endif()
endforeach()

# 添加 src 子模块
if(EXISTS ${PROJECT_SOURCE_DIR}/src)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src)
endif()

# 添加 strategy 子模块
if(EXISTS ${PROJECT_SOURCE_DIR}/strategy)
    add_subdirectory(${PROJECT_SOURCE_DIR}/strategy)
endif()

# IF(EXISTS ${PROJECT_SOURCE_DIR}/Relaxquant_Release)
# FILE(COPY ${PROJECT_SOURCE_DIR}/include DESTINATION ${PROJECT_SOURCE_DIR}/Relaxquant_Release)
# FILE(GLOB HERMES_LIBS ${PROJECT_SOURCE_DIR}/ext/Hermes_Release/lib/lib*.so)
# FILE(COPY ${HERMES_LIBS}
# DESTINATION ${PROJECT_SOURCE_DIR}/Relaxquant_Release/dependencies)
# FILE(COPY ${PROJECT_SOURCE_DIR}/ext/Hermes_Release/include
# DESTINATION ${PROJECT_SOURCE_DIR}/Relaxquant_Release/Hermes_Release)
# IF (EXISTS ${PROJECT_SOURCE_DIR}/strategy)
# FILE(GLOB _tmp_c "${PROJECT_SOURCE_DIR}/strategy/*.c")
# FILE(GLOB _tmp_cXX "${PROJECT_SOURCE_DIR}/strategy/*.cpp")
# SET(SRCS "${_tmp_h};${_tmp_cxx}")
# FILE(COPY ${SRCS} DESTINATION ${PROJECT_SOURCE_DIR}/Relaxquant_Release/src)
# ENDIF()
# ENFIF()