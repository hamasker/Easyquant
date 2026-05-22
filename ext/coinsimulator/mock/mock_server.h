#pragma once

#include "trade/trade_server.h"

#include <vector>

BEGIN_NOVA_NAMESPACE(trade)

class TradeEngine;

void InitMockFramework();
TradeServer *GetMockServer();

// 在 Initialize() 前注册 engine
void RegisterMockEngine(TradeEngine *engine);

END_NOVA_NAMESPACE(trade)
