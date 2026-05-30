#!/usr/bin/env bash
# 在 detached screen 里启动 agent（可选，适合长期挂着）
# 用法: ./scripts/start-wechat-agent-screen.sh eq
set -euo pipefail

AGENT="${1:?用法: $0 <agent>}"
ROOT="/home/baser/Working/MX/Easyquant"
SESSION="wechat-${AGENT}"

if screen -ls | grep -q "[.]${SESSION}[[:space:]]"; then
  echo "screen ${SESSION} 已存在，先: screen -S ${SESSION} -X quit" >&2
  exit 1
fi

screen -S "$SESSION" -dm bash -lc "cd '${ROOT}' && ./scripts/start-wechat-agent.sh '${AGENT}'"
sleep 3
screen -S "$SESSION" -X stuff $'\r'
echo "已启动 screen -S ${SESSION}，attach: screen -r ${SESSION}"
