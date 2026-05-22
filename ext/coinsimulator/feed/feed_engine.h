#pragma once

#include "nova_api_quote_struct.h"
#include "trade/trade_engine.h"

#include <cstdint>

BEGIN_NOVA_NAMESPACE(trade)

/**
 * FeedEngine — 行情数据源抽象接口。
 *
 * Mock/Prod:  实时 WebSocket 行情
 * Backtest:   本地 CSV 文件回放
 */
class FeedEngine {
public:
  virtual ~FeedEngine() = default;

  // 初始化, 读取 config 中的 Quote.backtest / Quote.ws 等配置
  virtual bool Initialize(const nova::base::Config &cfg) = 0;

  // 主循环轮询: 获取下一批数据, 推入 DataInfoManager 并触发策略回调
  // 返回 true 表示还有数据, false 表示数据源已结束
  virtual bool Poll() = 0;

  // 停止数据源
  virtual void Stop() = 0;
};

END_NOVA_NAMESPACE(trade)
