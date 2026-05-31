NOVA_CMAKE_ENTER()

file(GLOB taking_src
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(TakingDemo SHARED ${taking_src})

set_target_properties(TakingDemo PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/lib"
    LIBRARY_OUTPUT_NAME "TakingDemo"
)

# 自动查找并添加所有 include 目录
set(INCLUDE_DIRS "")
# 添加项目根目录的 include 及其所有子目录（递归）
if(EXISTS "${PROJECT_SOURCE_DIR}/include")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include")
    # 显式添加 common 和 util_tools 子目录（确保 data_currency.h 和 str_util.h 等头文件可以被找到）
    if(EXISTS "${PROJECT_SOURCE_DIR}/include/common")
        list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/common")
    endif()
    if(EXISTS "${PROJECT_SOURCE_DIR}/include/util_tools")
        list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include/util_tools")
    endif()
    # 递归添加 include 目录下的所有子目录
    file(GLOB_RECURSE INCLUDE_SUBDIRS "${PROJECT_SOURCE_DIR}/include/*")
    foreach(SUBDIR ${INCLUDE_SUBDIRS})
        if(IS_DIRECTORY "${SUBDIR}")
            list(APPEND INCLUDE_DIRS "${SUBDIR}")
        endif()
    endforeach()
endif()
# 查找 ext 目录下的所有 include 目录
file(GLOB INCLUDE_DIR_LIST "${PROJECT_SOURCE_DIR}/ext/*/include")
file(GLOB INCLUDE_DIR_LIST2 "${PROJECT_SOURCE_DIR}/ext/*/*/include")
file(GLOB INCLUDE_DIR_LIST3 "${PROJECT_SOURCE_DIR}/ext/lib/*/include")
list(APPEND INCLUDE_DIR_LIST ${INCLUDE_DIR_LIST2} ${INCLUDE_DIR_LIST3})
foreach(INCLUDE_DIR ${INCLUDE_DIR_LIST})
    get_filename_component(INCLUDE_DIR_ABS "${INCLUDE_DIR}" ABSOLUTE)
    # 排除 build 目录和隐藏目录
    if(NOT INCLUDE_DIR_ABS MATCHES "build" AND NOT INCLUDE_DIR_ABS MATCHES "\\.[^/]")
        list(APPEND INCLUDE_DIRS "${INCLUDE_DIR_ABS}")
        # 同时添加 include 目录下的所有子目录（递归）
        file(GLOB_RECURSE INCLUDE_SUBDIRS "${INCLUDE_DIR_ABS}/*")
        foreach(SUBDIR ${INCLUDE_SUBDIRS})
            if(IS_DIRECTORY "${SUBDIR}")
                list(APPEND INCLUDE_DIRS "${SUBDIR}")
            endif()
        endforeach()
    endif()
endforeach()
# 显式添加 NovaCoin2 的 include 目录（确保 API 头文件可以被找到）
if(EXISTS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/api")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/quote")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/trade")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/symbol_translator")
endif()

# 显式添加 NovaBase 的 base 和 tpl 子目录（确保 base_util.h 和 nlohmann_json 等头文件可以被找到）
if(EXISTS "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/base")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/base")
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/tpl")
    list(APPEND INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/tpl")
endif()

# 去重并排序
list(REMOVE_DUPLICATES INCLUDE_DIRS)
list(SORT INCLUDE_DIRS)
# 调试输出（可以看到找到的目录）
message(STATUS "TakingDemo include directories: ${INCLUDE_DIRS}")
# 添加包含路径（使用 PUBLIC 以便其他包含 data.h 的目标也能找到这些头文件）
target_include_directories(TakingDemo PUBLIC ${INCLUDE_DIRS})

# 与 coinrunner 一致：轻量日志（直接写文件），避免 LoggerServer 析构时 LoopAll 排空卡死
target_compile_definitions(TakingDemo PRIVATE FMT_HEADER_ONLY NOVA_BASE_DISABLE_LOGGER)

NOVA_CMAKE_LEAVE()
