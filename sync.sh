#!/bin/bash
# Easyquant 同步脚本 — 用于东京服务器部署
# 用法: ssh tokyo /opt/Easyquant/sync.sh

set -e
cd /opt/Easyquant

echo "=== git pull ==="
git fetch origin
git reset --hard origin/master

echo "=== compile ==="
bash compile.sh

echo "=== done ==="
