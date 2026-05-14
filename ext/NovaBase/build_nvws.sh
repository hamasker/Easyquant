#!/bin/bash

# NovaBase WebSocket 编译脚本
# 用于快速编译和测试 nvws 库

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}NovaBase WebSocket 编译脚本${NC}"
echo -e "${GREEN}========================================${NC}"

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# 检查依赖
echo -e "\n${YELLOW}[1/5] 检查依赖...${NC}"

check_dependency() {
    if ! pkg-config --exists $1 2>/dev/null; then
        echo -e "${RED}错误: 找不到 $1${NC}"
        echo -e "${YELLOW}请安装: sudo apt-get install $2${NC}"
        return 1
    else
        echo -e "${GREEN}✓ $1 已安装${NC}"
        return 0
    fi
}

DEPS_OK=true
check_dependency "zlib" "zlib1g-dev" || DEPS_OK=false
check_dependency "openssl" "libssl-dev" || DEPS_OK=false

if [ "$DEPS_OK" = false ]; then
    echo -e "\n${RED}请先安装缺失的依赖项${NC}"
    exit 1
fi

# 创建构建目录
echo -e "\n${YELLOW}[2/5] 创建构建目录...${NC}"
mkdir -p build
cd build

# 运行 CMake
echo -e "\n${YELLOW}[3/5] 运行 CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
echo -e "\n${YELLOW}[4/5] 编译...${NC}"
make -j$(nproc)

# 检查编译结果
echo -e "\n${YELLOW}[5/5] 检查编译结果...${NC}"

if [ -f "lib/libnvws.a" ]; then
    echo -e "${GREEN}✓ libnvws.a 编译成功${NC}"
    ls -lh lib/libnvws.a
else
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi

# 显示库信息
echo -e "\n${GREEN}========================================${NC}"
echo -e "${GREEN}编译完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "库文件位置: ${YELLOW}$SCRIPT_DIR/build/lib/libnvws.a${NC}"
echo -e "\n使用方法："
echo -e "  在你的项目中链接: ${YELLOW}-L$SCRIPT_DIR/build/lib -lnvws -lssl -lcrypto -lz -lpthread${NC}"
echo -e "  包含头文件: ${YELLOW}-I$SCRIPT_DIR/include${NC}"
echo -e "\n文档位置："
echo -e "  ${YELLOW}$SCRIPT_DIR/src/nvws/README.md${NC}"
echo -e "  ${YELLOW}$SCRIPT_DIR/src/nvws/IMPLEMENTATION_SUMMARY.md${NC}"

echo -e "\n${GREEN}完成！${NC}\n"

