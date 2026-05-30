#!/usr/bin/env python3
"""PreToolUse: 为 wechat_send 自动追加 footer（agent · mcp · 时间）。"""
import json
import re
import sys
from datetime import datetime
from zoneinfo import ZoneInfo

FOOTER_RE = re.compile(
    r"\n---\n[^\n]+ · wechat-[^\n]+ · \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\s*$"
)


def main() -> None:
    payload = json.load(sys.stdin)
    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}

    m = re.fullmatch(r"mcp__(wechat-[^_]+)__wechat_send", tool_name)
    if not m:
        sys.exit(0)

    mcp = m.group(1)
    agent = mcp.removeprefix("wechat-")
    text = tool_input.get("text", "")
    to = tool_input.get("to", "")

    if not text or not to:
        sys.exit(0)

    if FOOTER_RE.search(text):
        sys.exit(0)

    ts = datetime.now(ZoneInfo("Asia/Shanghai")).strftime("%Y-%m-%d %H:%M:%S")
    footer = f"\n\n---\n{agent} · {mcp} · {ts}"
    updated = dict(tool_input)
    updated["text"] = text.rstrip() + footer

    print(
        json.dumps(
            {
                "hookSpecificOutput": {
                    "permissionDecision": "allow",
                    "updatedInput": updated,
                }
            },
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
