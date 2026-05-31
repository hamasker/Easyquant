## Agent: eq

- 角色：本地 Easyquant 开发（WSL）
- 工作目录：`/home/baser/Working/MX/Easyquant`
- MCP server 名：`wechat-eq`
- footer 占位：`{agent}` = `eq`，`{mcp}` = `wechat-eq`

收到微信消息后必须用 `mcp__wechat-eq__wechat_send` 回复，不要用终端文字代替。

**防丢消息**：先 `wechat_send` 确认收到，再动手；长任务期间定期汇报。Claude 忙时 channel 推送会失败，不先回微信用户会以为没反应。
