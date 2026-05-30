#!/usr/bin/env bash
# 启动单个 WeChat Claude agent（eq 或 tokyo）
# 用法: ./scripts/start-wechat-agent.sh eq
#       ./scripts/start-wechat-agent.sh tokyo
set -euo pipefail

AGENT="${1:?用法: $0 eq|tokyo}"
ROOT="/home/baser/Working/MX/Easyquant"
cd "$ROOT"

case "$AGENT" in
  eq)
    MCP='{"mcpServers":{"wechat-eq":{"command":"npx","args":["@paean-ai/wechat-mcp","serve","--agent","eq","--mode","channel"]}}}'
    CHANNEL="wechat-eq"
    ;;
  tokyo)
    MCP='{"mcpServers":{"wechat-tokyo":{"command":"npx","args":["@paean-ai/wechat-mcp","serve","--agent","tokyo","--mode","channel"]}}}'
    CHANNEL="wechat-tokyo"
    ;;
  *)
    echo "未知 agent: $AGENT (仅支持 eq / tokyo)" >&2
    exit 1
    ;;
esac

# 确保 daemon 在跑
wechat-mcp status >/dev/null 2>&1 || nohup wechat-mcp daemon >/tmp/wechat-daemon.log 2>&1 &

echo "启动 @${AGENT} …"
echo "若出现 development channel 确认界面，选 1 并回车（只需一次）"
echo ""

exec claude --dangerously-skip-permissions \
  --strict-mcp-config \
  --mcp-config "$MCP" \
  --dangerously-load-development-channels "server:${CHANNEL}"
