#pragma once
#include "common/data.h"
#include "configs/configs.h"
using StrategyConfig = Configs;

class FairPriceGenerator {
public:
  using Depth = NovaCoinDepth;

  FairPriceGenerator(
      std::unordered_map<data::currency, data::fair_price_data> &fps_map,
      data::InstrumentData &InstData_, StrategyConfig &CFG_)
      : fps_map_(fps_map), InstData_(InstData_),
        id_map(InstData_.IM.inststr2id_), depth_map(InstData_.depth_map),
        bbo_map(InstData_.bbo_map), bar_map(InstData_.bar_map),
        vol_map(InstData_.vol_map), CFG_(CFG_) {}

  // 公共接口，用于生成 fair price
  bool update(const int64_t ts);
  bool update_volatilities(data::VolatilityMethod method);

private:
  void calculate_fp_usdt();
  void calculate_fp_usdc();
  void calculate_fp_usd();
  void calculate_fp_forex(const data::currency &currency);
  void calculate_fp_digital(const data::currency &currency);
  void update_fp_insts();

  std::unordered_map<data::currency, data::fair_price_data> &fps_map_;
  data::InstrumentData &InstData_;
  decltype(InstData_.IM.inststr2id_) &id_map;
  decltype(InstData_.depth_map) &depth_map;
  decltype(InstData_.bbo_map) &bbo_map;
  decltype(InstData_.bar_map) &bar_map;
  decltype(InstData_.vol_map) &vol_map;
  StrategyConfig &CFG_;

  int64_t ts_tmp = 0;       // 当前计算时间戳
  int fp_update_times = 0;
  bool first_cal = true;
};