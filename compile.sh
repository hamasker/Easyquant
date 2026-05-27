#!/bin/bash

clear

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# rm -rf build
# mkdir build && cd build
cd build

cmake .. -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j2

# 复制 compile_commands.json 到项目根目录
cp compile_commands.json ..
