## 微信回复格式

footer 由 **PreToolUse hook 自动注入**到每条 `wechat_send`，无需手动拼接。
格式：`---\n{agent} · {mcp} · {time}`（Asia/Shanghai）

你只需写正文内容；hook 会在发送前追加 footer。
