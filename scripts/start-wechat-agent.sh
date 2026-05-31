#!/usr/bin/env bash
# 启动 WeChat Claude agent
# 用法:
#   ./scripts/start-wechat-agent.sh          # 列出所有 agent
#   ./scripts/start-wechat-agent.sh eq
#   ./scripts/start-wechat-agent.sh tk
set -euo pipefail

ROOT="/home/baser/Working/MX/Easyquant"
MANIFEST="${ROOT}/.claude/agents/manifest.json"
AGENT="${1:-}"

cd "$ROOT"

list_agents() {
  python3 - "$MANIFEST" <<'PY'
import json, sys
manifest = json.load(open(sys.argv[1]))
print("可用 WeChat agent（微信里 @名字 路由）：\n")
for name, cfg in manifest["agents"].items():
    desc = cfg.get("description", "")
    print(f"  {name:8}  MCP={cfg['mcpServer']:14}  微信 @{cfg['wechatAgentId']}  {desc}")
print("\n新增 agent 见 .claude/agents/manifest.json + 复制 _template.system.md")
PY
}

if [[ -z "$AGENT" ]]; then
  list_agents
  exit 0
fi

if ! CFG_JSON=$(python3 - "$MANIFEST" "$AGENT" <<'PY'
import json, sys
manifest_path, agent = sys.argv[1:3]
manifest = json.load(open(manifest_path))
if agent not in manifest["agents"]:
    known = ", ".join(manifest["agents"])
    sys.stderr.write(f"未知 agent: {agent}\n已注册: {known}\n")
    sys.exit(1)

cfg = manifest["agents"][agent]
wechat_id = cfg["wechatAgentId"]
mcp_server = cfg["mcpServer"]
out = {
    "mcp": {
        "mcpServers": {
            mcp_server: {
                "command": "npx",
                "args": ["@paean-ai/wechat-mcp", "serve", "--agent", wechat_id, "--mode", "channel"]
            }
        }
    },
    "channel": mcp_server,
    "model": cfg.get("model") or manifest.get("defaultModel") or "",
    "settings": cfg.get("settings") or "",
    "prompts": manifest.get("sharedPrompts", []) + [cfg["systemPrompt"]],
}
print(json.dumps(out, separators=(",", ":")))
PY
); then
  list_agents >&2
  exit 1
fi

MCP=$(python3 -c "import json,sys; print(json.dumps(json.loads(sys.argv[1])['mcp'], separators=(',', ':')))" "$CFG_JSON")
CHANNEL=$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['channel'])" "$CFG_JSON")
MODEL=$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['model'])" "$CFG_JSON")
SETTINGS=$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['settings'])" "$CFG_JSON")
mapfile -t PROMPTS < <(python3 -c "import json,sys; print('\n'.join(json.loads(sys.argv[1])['prompts']))" "$CFG_JSON")

wechat-mcp status >/dev/null 2>&1 || nohup wechat-mcp daemon >/tmp/wechat-daemon.log 2>&1 &

echo "启动 @${AGENT} (MCP=${CHANNEL}) …"
echo "若出现 development channel 确认界面，选 1 并回车（只需一次）"
echo ""

ARGS=(
  --dangerously-skip-permissions
  --strict-mcp-config
  --mcp-config "$MCP"
  --dangerously-load-development-channels "server:${CHANNEL}"
)
[[ -n "$SETTINGS" && -f "$SETTINGS" ]] && ARGS+=(--settings "$SETTINGS")
[[ -n "$MODEL" ]] && ARGS+=(--model "$MODEL")
for p in "${PROMPTS[@]}"; do
  [[ -n "$p" && -f "$p" ]] && ARGS+=(--append-system-prompt-file "$p")
done

exec claude "${ARGS[@]}"
