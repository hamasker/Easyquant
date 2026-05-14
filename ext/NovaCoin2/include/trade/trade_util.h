#pragma once

#include "trade/trade_struct.h"

BEGIN_NOVA_NAMESPACE(trade)
inline bool isSideBuy(NOVA_SIDE_TYPE side) { return side == NOVA_SIDE_BUY; }
inline bool isSideSell(NOVA_SIDE_TYPE side) { return side == NOVA_SIDE_SELL; }
inline bool isPositionOpen(NOVA_POSITION_EFFECT_TYPE pos) {
  return pos == NOVA_POSITION_EFFECT_OPEN;
}
inline bool isPositionClose(NOVA_POSITION_EFFECT_TYPE pos) {
  return pos == NOVA_POSITION_EFFECT_CLOSE;
}

inline bool isSideBuy(const NovaOrderDetail *order) {
  return isSideBuy(order->order.side);
}
inline bool isSideSell(const NovaOrderDetail *order) {
  return isSideSell(order->order.side);
}

END_NOVA_NAMESPACE(trade)