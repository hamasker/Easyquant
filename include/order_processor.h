#ifndef ORDER_PROCESSOR_H
#define ORDER_PROCESSOR_H

#include "common/data.h"
#include "configs/strategy_config.h"
#include <cstdint>

// 声明 OrderProcessor 类
class OrderProcessor {
public:
  using Depth = NovaCoinDepth;
  // 构造函数
  // OrderProcessor() = default;
  OrderProcessor(
      data::InstrumentData &InstData_, StrategyConfig &CFG_,
      const std::unordered_map<data::currency, data::fair_price_data> &fps_map,
      const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
      const int64_t &global_ts_)
      : InstData_(InstData_), id_map(InstData_.IM.inststr2id_),
        depth_map(InstData_.depth_map), vol_map(InstData_.vol_map),
        order_map(InstData_.order_map), CFG_(CFG_), fps_map_(fps_map),
        BlcMng_(BlcMng_), global_ts_(global_ts_) {}

  // 公有方法
  void Init() {
    if (!CFG_.backtest)
      ob_level = 100;
    limit_usd = CFG_.limit_usd;
  }
  data::fetch_orders_data fetch_orders(const data::currency &currency) const;

  // std::unordered_map<std::string, double> balances;

private:
  // 私有常量
  double limit_usd; // 常量，不可改变
  const int max_order_num = 3;
  double currency_collateral_ratio = 1.0;
  double balance_adj = 0.999;
  double BP = 0.0001;
  int ob_level = 25;

  // 私有变量
  data::InstrumentData &InstData_;
  decltype(InstData_.IM.inststr2id_) &id_map;
  decltype(InstData_.depth_map) &depth_map;
  decltype(InstData_.vol_map) &vol_map;
  decltype(InstData_.order_map) &order_map;
  StrategyConfig &CFG_;
  const std::unordered_map<data::currency, data::fair_price_data> &fps_map_;
  const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_;
  const int64_t &global_ts_;

  // 私有函数声明

  double _get_fp(const data::currency &currency,
                 data::InstrumentComponent &IC) const;

  // return snapshots, volatilities, open_orders
  std::tuple<std::unordered_map<data::UniInstID, data::snapshot_data>,
             //  std::unordered_map<data::UniInstID, data::pair_data>,
             std::unordered_map<data::UniInstID, std::vector<data::order_data>>>
  _get_matching_currency_components(const data::currency &currency) const;

  // return balances_credit, costs_limit, deltas
  std::tuple<std::unordered_map<data::currency, double>,
             std::unordered_map<data::currency, double>,
             std::unordered_map<data::currency, double>>
  _adjust_balances() const;

  std::unordered_map<data::UniInstID, data::snapshot_data> _process_snapshots(
      const data::currency &currency,
      std::unordered_map<data::UniInstID, data::snapshot_data>
          &matching_snapshots,
      const std::unordered_map<data::UniInstID, std::vector<data::order_data>>
          &matching_open_orders) const;

  double _get_prob(const std::vector<std::vector<double>> &prob_params,
                   const double &quantity, const double &depth,
                   const double &vp) const;

  // 从prob反推quantity
  // 注意：_get_prob返回的是log10(概率)，实际概率 = pow(10, log10_prob)
  // 此函数接收实际概率值prob，内部会转换为log10_prob进行反推
  double
  _get_quantity_from_prob(const std::vector<std::vector<double>> &prob_params,
                          const double &prob, const double &depth,
                          const double &vp) const;

  void _sift_points_too_far(data::order_cache &OC) const;

  void _sift_points_backward(data::order_cache &OC) const;

  double _get_order_margin_no_adjust(
      const std::vector<std::vector<double>> &prob_params, const double &depth,
      const double &quantity, const double &ret, const double &vp) const;

  double _get_order_margin(data::order_cache &OC, const double &price,
                           const double &depth, const double &quantity,
                           const double &ret) const;

  void _sift_points_forward(data::order_cache &OC) const;

  std::vector<data::inner_point_data>
  _cal_valid_points_quantity(data::order_cache &OC) const;

  std::pair<double, double>
  _get_point_inter(data::order_cache &OC, const std::array<double, 2> &point1,
                   const std::array<double, 2> &point2,
                   const double &max_available) const;

  // int _check_costs(const data::currency &currency,
  //                  const std::unordered_map<std::string, double>
  //                  &costs_dict, const std::unordered_map<data::currency,
  //                  double> &balances_credit, const
  //                  std::unordered_map<data::currency, double>
  //                  &costs_limit);

  data::fetch_orders_data
  _search_reasonable_margin(data::inner_point_data_cache &IPDC) const;

  std::tuple<double,                                      // costs_sum
             std::unordered_map<data::UniInstID, double>> // costs_dict
  //  std::unordered_map<data::UniInstID, double>, // add_dict
  //  std::unordered_map<data::UniInstID, double>> // costs_margin_dict
  _get_costs_sum(const double &ret, data::inner_point_data_cache &IPDC) const;

  double _get_oppsite_costs(
      const data::currency &currency,
      const std::unordered_map<data::currency, double> &costs_limit,
      const std::unordered_map<data::UniInstID,
                               std::vector<data::inner_point_data>>
          &inter_points_dict) const;

  void _generate_orders(
      const data::currency &currency, data::inner_point_data_cache &IPDC,
      data::fetch_orders_data &output,
      const std::unordered_map<data::UniInstID, std::vector<data::order_data>>
          &open_orders) const;

  void
  _adjust_costs_dict(std::unordered_map<data::UniInstID, double> &costs_dict,
                     const double &thre_cost) const;
};
#endif // ORDER_PROCESSOR_H