#pragma once
#include "common/data.h"
#include "configs/configs.h"
using StrategyConfig = Configs;
#include <deque>
#include <unordered_map>
#include <utility>

class FairPriceGenerator {
public:
  using Depth = NovaCoinDepth;

  FairPriceGenerator(
      std::unordered_map<data::currency, data::fair_price_data> &fps_map,
      data::InstrumentData &InstData_, StrategyConfig &CFG_)
      : fps_map_(fps_map), InstData_(InstData_),
        id_map(InstData_.IM.inststr2id_), depth_map(InstData_.depth_map),
        bbo_map(InstData_.bbo_map), bar_map(InstData_.bar_map),
        vol_map(InstData_.vol_map), CFG_(CFG_) {
    ts_vec.resize(1, 0.0);
  }

  // 公共接口，用于生成 fair price
  bool update(const int64_t ts);
  void update_volatilities_hl();
  void update_slope(data::slope_data &vd, const double &fp_bid,
                    const double &fp_ask, const std::string &pair_str);
  bool update_volatilities_fp(data::VolatilityMethod method);

private:
  static constexpr double ema_fast_alpha = 0.1;
  static constexpr double ema_fast_alpha2 = 0.01;
  static constexpr double ema_slow_alpha = 0.0001111;

  void calculate_fp_usdt_old();
  void calculate_fp_usdt();
  void calculate_fp_usdc();
  void calculate_fp_usd();
  void calculate_fp_forex(const data::currency &currency);
  void calculate_fp_forex_old(const data::currency &currency);
  void calculate_fp_digital(const data::currency &currency);
  void calculate_fp_digital_old(const data::currency &currency);
  void calculate_fp_digital_swap(const data::currency &currency);
  void update_fp_insts();

  std::vector<double> ts_vec;
  double ts_tmp;
  double ts_forex_update = 0;
  double fp_bid_median;
  double fp_ask_median;
  double volume_threshold = 0.0;
  int fp_update_times = 0;
  bool first_cal = true;
  bool cal_time = false;

  std::unordered_map<data::currency, data::fair_price_data> &fps_map_;
  data::InstrumentData &InstData_;
  decltype(InstData_.IM.inststr2id_) &id_map;
  decltype(InstData_.depth_map) &depth_map;
  decltype(InstData_.bbo_map) &bbo_map;
  decltype(InstData_.bar_map) &bar_map;
  decltype(InstData_.vol_map) &vol_map;
  StrategyConfig &CFG_;
};