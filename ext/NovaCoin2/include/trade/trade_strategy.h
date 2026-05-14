#pragma once

#include "base/base_config.h"

#include "quote/quote_struct.h"
#include "trade/trade_portfolio.h"
#include "trade/trade_struct.h"

USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)

BEGIN_NOVA_NAMESPACE(trade)

class Strategy {
public:
  using Depth = NovaCoinDepth;
  using DEPTHLVN = NovaCoinDepthLVN;
  using Trade = NovaCoinTrade;
  using BBO = NovaCoinBBO;
  using Bar = NovaCoinBar;

public:
  Strategy() : name_("BaseStrategy") {}
  virtual ~Strategy() = default;

  virtual bool OnStrategyStart() { return true; }
  virtual void OnStrategyStop() {}

  virtual void OnDepth(const Depth *d, const SecurityPosition *postion) {}

public:
  void set_name(const char *name) { name_ = name; }
  const char *name() const { return name_.c_str(); }
  void set_core(int32_t core) { core_ = core; }
  int32_t core() const { return core_; }

private:
  std::string name_;
  int32_t core_ = -1;
};

END_NOVA_NAMESPACE(trade)