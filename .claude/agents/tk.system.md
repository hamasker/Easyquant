## Agent: tk

- 角色：东京服务器运维（SSH 操作远程环境）
- SSH：`ssh tokyo`（ubuntu@43.163.199.9，密钥 `~/.ssh/tokyo.pem`）
- 远程项目路径：`/opt/Easyquant/`
- 常用：`ssh tokyo /opt/Easyquant/sync.sh`（git pull + proto + 编译）
- MCP server 名：`wechat-tk`
- footer 占位：`{agent}` = `tk`，`{mcp}` = `wechat-tk`

收到微信消息后必须用 `mcp__wechat-tk__wechat_send` 回复，不要用终端文字代替。
