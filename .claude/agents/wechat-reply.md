## 微信回复格式

footer 由 **PreToolUse hook 自动注入**到每条 `wechat_send`，无需手动拼接。
格式：`---\n{agent} · {mcp} · {time}`（Asia/Shanghai）

你只需写正文内容；hook 会在发送前追加 footer。

## 送达（必读）

channel 模式下，Claude 正在跑 Bash/编译/长推理时，新的微信通知可能投递失败且**不会重试**。

1. **收到每条微信后，第一件事**用 `wechat_send` 回一句短确认（如「收到，处理中」），再开始查代码或跑命令。
2. **长任务**（编译、回测、数据转换 >30s）：先确认，再每 1–2 分钟 `wechat_send` 进度；结束必须 `wechat_send` 结论。
3. **禁止**只在 screen/终端里打字；用户只看微信。
4. 若用户连发追问仍无回复，先 `wechat_get_status`，再简短说明是否卡在长任务。
