#!/usr/bin/env bash
# 重启 WeChat daemon + 指定 agent 的 screen 会话（解决 channel 丢消息 / daemon 断连）
# 用法:
#   ./scripts/restart-wechat-agent.sh       # 默认 eq
#   ./scripts/restart-wechat-agent.sh tk
set -euo pipefail

AGENT="${1:-eq}"
ROOT="/home/baser/Working/MX/Easyquant"
SESSION="wechat-${AGENT}"

cd "$ROOT"

if ! python3 - "$ROOT/.claude/agents/manifest.json" "$AGENT" <<'PY'
import json, sys
manifest = json.load(open(sys.argv[1]))
sys.exit(0 if sys.argv[2] in manifest["agents"] else 1)
PY
then
  echo "未知 agent: ${AGENT}，见 .claude/agents/manifest.json" >&2
  exit 1
fi

echo "==> 停止 screen ${SESSION}（若存在）"
if screen -ls | grep -q "[.]${SESSION}[[:space:]]"; then
  screen -S "$SESSION" -X quit || true
  sleep 1
fi

echo "==> 重启 wechat-mcp daemon"
if wechat-mcp status >/dev/null 2>&1; then
  wechat-mcp stop || true
  sleep 1
fi
nohup wechat-mcp daemon >/tmp/wechat-daemon.log 2>&1 &
for _ in $(seq 1 15); do
  if wechat-mcp status >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
if ! wechat-mcp status >/dev/null 2>&1; then
  echo "daemon 启动失败，查看 /tmp/wechat-daemon.log" >&2
  exit 1
fi

echo "==> 启动 agent @${AGENT}"
exec "${ROOT}/scripts/start-wechat-agent-screen.sh" "$AGENT"
