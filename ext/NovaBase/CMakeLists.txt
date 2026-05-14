# /***************************
# *
# *
# *
# *
# ***************************

cmake_minimum_required(VERSION 3.16...3.27)

# set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/gcc/bin/gcc)
# set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/gcc/bin/g++)
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

# 设置 C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NOVA_BASE_PROJECT OFF)

if(CMAKE_CURRENT_LIST_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(NovaBase)
    set(NOVA_BASE_PROJECT ON)
endif()

# check if NOVA is being used directly or via add_subdirectory
set(NOVA_MASTER_PROJECT OFF)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(NOVA_MASTER_PROJECT ON)
endif()

if(NOVA_BASE_PROJECT)
    # 作为独立顶层工程时，只保留 BASE 角色，避免双重角色导致重复 add_subdirectory
    set(NOVA_MASTER_PROJECT OFF)
endif()

if(NOT NOVA_BASE_PROJECT AND NOT NOVA_MASTER_PROJECT)
    # 作为子目录被上层项目引入时，默认视为 MASTER 项目环境
    set(NOVA_MASTER_PROJECT ON)
endif()

if(NOVA_BASE_PROJECT OR NOVA_MASTER_PROJECT)
    MESSAGE("NOVA_BASE_PROJECT:${NOVA_BASE_PROJECT}")
    MESSAGE("NOVA_MASTER_PROJECT:${NOVA_MASTER_PROJECT}")
endif()

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
INCLUDE(common)

SET(CMAKE_CONFIGURATION_TYPES "Debug;Release")
SET(ENV_C_FLAGS $ENV{CFLAGS})
SET(ENV_CXX_FLAGS $ENV{CXXFLAGS})

SET(BUILDTYPE_DOCSTRING "Choose the type of build, option are Debug Release")

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE "Debug" CACHE STRING ${BUILDTYPE_DOCSTRING} FORCE)
    MESSAGE(STATUS "CAMKE_BUILD_TYPE not set. Defaulting to 'Debug'.")
ENDIF()

SET(ALLOWED_BUILD_TYPES "Debug" "Release")
SET(IS_VALID_BUILD_TYPE FALSE)

FOREACH(type ${ALLOWED_BUILD_TYPES})
    IF(CMAKE_BUILD_TYPE STREQUAL type)
        SET(IS_VALID_BUILD_TYPE TRUE)
        BREAK()
    ENDIF()
ENDFOREACH()

if(NOT IS_VALID_BUILD_TYPE)
    message(FATAL_ERROR "Invalid CMAKE_BUILD_TYPE: '${CMAKE_BUILD_TYPE}'. Allowed types are: ${ALLOWED_BUILD_TYPES}.")
endif()

# nova cmake options
OPTION(NOVA_ENABLE_TEST "Enable Unit Testing" ${NOVA_MASTER_PROJECT})
OPTION(NOVA_FORCE_X64 "Force x64 platform; Ensure 64 bit" ON)

OPTION(NOVA_NO_OVERHEAD "Disable RTTI and Exception" OFF)
OPTION(NOVA_AGGRESSIVE_OPTIMIZE "Enable aggressive optimizations" OFF)

OPTION(NOVA_ENABLE_VALGRIND "Enable valgrind instrumentation" OFF)
OPTION(NOVA_ENABLE_GCOV "Enable gcov (debug, linux builds only)" OFF)
OPTION(NOVA_ENABLE_GPROF "Enable gprof (optimized, Linux builds only)" OFF)

OPTION(NOVA_ENABLE_ASAN "Enable address sanitizer (debug, Linux builds only)" OFF)
OPTION(NOVA_ENABLE_TSAN "Enable thread sanitizer (debug, Linux builds only)" OFF)
OPTION(NOVA_ENABLE_MSAN "Enable memory sanitizer (debug, Linux builds only)" OFF)
OPTION(NOVA_ENABLE_UBSAN "Enable undefined behavior sanitizer (debug, Linux builds only)" OFF)

OPTION(NOVA_ENABLE_TEST_BUILD "Enable build some test cases to check compiler and flags" OFF)

# include directories
SET(NOVA_HOME ${CMAKE_SOURCE_DIR})
SET(NOVA_BIN_DIR "${NOVA_HOME}/bin")
SET(NOVA_LIB_DIR "${NOVA_HOME}/lib")
SET(NOVA_INC_DIR "${NOVA_HOME}/include")
SET(NOVA_EXT_DIR "${NOVA_HOME}/ext")
SET(NOVA_DOC_DIR "${NOVA_HOME}/doc")
SET(NOVA_CMAKE_SCRIPT_DIR "${NOVA_HOME}/cmake")
SET(NOVA_INSTALL_DIR "${NOVA_HOME}/${CMAKE_PROJECT_NAME}_Release")

IF(NOT NOVA_BASE_PROJECT)
    # add root project
    NOVA_SET_COMMON_DIRECTORIES(${CMAKE_SOURCE_DIR})
ENDIF()

NOVA_SET_COMMON_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR})

FOREACH(_base
    ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_VERSION}-${CMAKE_SYSTEM_PROCESSOR}
    ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_VERSION}
    ${CMAKE_SYSTEM_NAME})
    SET(_file ${CMAKE_CURRENT_LIST_DIR}/cmake/os/${_base}.cmake)

    IF(EXISTS ${_file})
        INCLUDE(${_file})
        BREAK()
    ENDIF()
ENDFOREACH()

IF(DEFINED ENV{CPPFLAGS})
    ADD_DEFINTIONS($ENV{CPPFLAGS})
ENDIF()

SET(NOVA_HOME_DIR ${CMAKE_SOURCE_DIR})
SET(NOVA_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

IF(NOT EXECUTABLE_OUTPUT_PATH)
    SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
ENDIF()

IF(NOT LIBRARY_OUTPUT_PATH)
    SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
ENDIF()

IF(NOT CMAKE_INSTALL_PREFIX)
    SET(CMAKE_INSTALL_PREFIX ${NOVA_INSTALL_DIR})
ENDIF()

if(NOVA_BASE_PROJECT)
    # 作为独立工程，仅添加 src；避免与 NOVA_ADD_SUBDIRS 重复
    add_subdirectory(src)
elseif(NOVA_MASTER_PROJECT)
    # 作为子模块，仅添加 src
    add_subdirectory(src)
    NOVA_INSTALL_PROJECT()
else()
    NOVA_ERROR("")
endif()