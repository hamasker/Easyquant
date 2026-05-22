# ============================================================
# coinrunner — Mock/Prod/Backtest 策略运行器
# 框架代码 → 静态库只编一次，strategy/ 变更时只重链
# ============================================================

# ========== 框架源文件 (很少改动) ==========
file(GLOB COINRUNNER_FRAMEWORK_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mock/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/feed/*.cpp"
)

# 框架 → 静态库
add_library(coinrunner_framework STATIC ${COINRUNNER_FRAMEWORK_SRCS})

# ========== 策略源文件 (频繁改动) ==========
file(GLOB STRATEGY_SRCS
    "${PROJECT_SOURCE_DIR}/strategy/*.cpp"
)

# ========== Include 路径 (框架和策略共用) ==========
set(COINRUNNER_INCLUDES
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/common"
    "${PROJECT_SOURCE_DIR}/include/util_tools"
    "${PROJECT_SOURCE_DIR}/include/configs"
    "${PROJECT_SOURCE_DIR}/ext/NovaBase/include"
    "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/base"
    "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/tpl"
    "${PROJECT_SOURCE_DIR}/ext/NovaBase/include/tpl/nvws"
    "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include"
    "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/api"
    "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/quote"
    "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/trade"
    "${PROJECT_SOURCE_DIR}/ext/NovaCoin2/include/symbol_translator"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

file(GLOB_RECURSE INCLUDE_SUBDIRS "${PROJECT_SOURCE_DIR}/include/*")
foreach(SUBDIR ${INCLUDE_SUBDIRS})
    if(IS_DIRECTORY "${SUBDIR}")
        list(APPEND COINRUNNER_INCLUDES "${SUBDIR}")
    endif()
endforeach()
list(REMOVE_DUPLICATES COINRUNNER_INCLUDES)

# ========== 可执行文件 ==========
add_executable(coinrunner ${STRATEGY_SRCS})

target_include_directories(coinrunner PRIVATE ${COINRUNNER_INCLUDES})
target_include_directories(coinrunner_framework PRIVATE ${COINRUNNER_INCLUDES})

# ========== 链接 ==========
target_link_libraries(coinrunner PRIVATE coinrunner_framework nvws
    ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto pthread)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# ========== 编译选项 ==========
foreach(tgt coinrunner coinrunner_framework)
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter -fPIC)
    target_compile_definitions(${tgt} PRIVATE FMT_HEADER_ONLY NOVA_BASE_DISABLE_LOGGER COINRUNNER_BUILD)
    target_compile_features(${tgt} PRIVATE cxx_std_20)
endforeach()

install(TARGETS coinrunner RUNTIME DESTINATION bin)
