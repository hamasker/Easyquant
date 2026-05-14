#pragma once

#include "common/data.h"
#include "config_reloader.h"
#include "data_process.h"
#include "fair_price_generator.h"
#include "nova_trader_api.h"
#include "order_processor.h"
#include "record.h"
#include <cstdint>

USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)
USE_NOVA_NAMESPACE(trade)

class TakingDemo : public StrategyApi {

public: // * 构造/析构函数
  TakingDemo()
      : StrategyApi(), account_position_(nullptr),
        fp_generator_(fps_map_, InstData_, CFG_),
        order_processor_(InstData_, CFG_, fps_map_, BlcMng_, global_ts) {}
  ~TakingDemo() override = default;

public: // * 各类方法
  bool on_init(const Config *cfg) override;
  void on_stop() override;
  void on_user_command(const char *msg) override { INFO_FLOG("{}", msg); }

  void on_depth(const Depth *dx, const SecurityPosition *position) override;
  void on_trade(const Trade *trade, const SecurityPosition *position) override;
  void on_bbo(const BBO *trade, const SecurityPosition *position) override;

  void on_order_accepted(const OrderTp *order,
                         const SecurityPosition *position) override;
  void on_order_update(const OrderTp *order,
                       const SecurityPosition *position) override;
  void on_order_cancelled(const OrderTp *tp, const SecurityPosition *position,
                          double qty_cancelled) override;
  void on_order_cancel_failed(const OrderTp *tp,
                              const SecurityPosition *position,
                              int32_t reason) override;
  void on_order_rejected(const OrderTp *order, const SecurityPosition *position,
                         int32_t reason) override;
  void on_order_amended(const OrderTp *order, const SecurityPosition *position,
                        int32_t reason) override;
  void on_datainfo(const DataInfoManager *datainfo, int32_t di,
                   const SecurityPosition *position) override;
  void on_order_done(const StrategyApi::OrderTp *tp,
                     const SecurityPosition *position,
                     NOVA_ORDER_STATUS done_status) override;
  void on_reminder(void *data, uint64_t cur_ns) override;
  void OnRspTradeInformation(TradeEngine *engine, nlohmann::json &rsp,
                             int req_id) override;

  void get_trade_engine();

  void init_global_variables();

  void get_assets();

  void subscribe_instruments(std::string inst_str, bool sub_spot = true);

  void preprocess_orders();

  void update_costs_tranfers();

  void process_orders();

  bool check_submit_available();

  int judge_submit_or_amend(
      const std::string &inst_str,
      const std::set<const data::OrderTp *> left_orders,
      std::unordered_map<uint32_t, data::operation_data> &operation_flags,
      const double &place_price, const double &place_qty, NOVA_SIDE_TYPE side);

  void process_held_orders(
      const std::unordered_map<data::currency, data::fetch_orders_data>
          &fetch_orders_map_);

  bool process_rebalance();

  void submit_orders();

  bool submit_hedge_orders();

  void submit_insert_orders(const data::InstrumentComponent &IC, int side);

  void submit_insert_orders(const data::currency &currency,
                            const int &flag_trend);

  void cancel_orders(int64_t ts = 0, std::string method = "normal");

  void process_negative_orders();

  void process_invalid_status_orders();

  void update_rate_limit();

  void do_calculations(int64_t current_ts);

  void judge_rebalance_taker(const std::string &judge_method);

  const DataInfoManager *last_data_info;

  StrategyConfig CFG_;
  int32_t last_di;

  // 配置自动重载器
  std::unique_ptr<nova::config::ConfigReloader> config_reloader_;

  // 数据记录器
  std::unique_ptr<record::Record> recorder_;

  bool flag_first = true;

private:
  int64_t callback_ts;
  int64_t last_local_ns = 0;
  int64_t global_ts = 0;
  int64_t backtest_init_ts;
  int64_t backtest_range_ts;
  int32_t reject_count_ = 0;
  int32_t cxl_count_ = 0;
  double progress_bar = 0.0;
  double total_trd_ = 0.0;
  double total_qty_ = 0.0;
  double total_usd_ = 0.0;
  double total_usd_digital = 0.0;
  double taker_fee = 0.0;
  bool flag_data_ready = false;
  bool flag_vol_ready = false;
  bool flag_rebalance = false;
  bool flag_hedge = false;
  int flag_data_valid = 0;
  double vol_multi_tmp;
  bool flag_circle_submit = false;
  bool disconnect_flag = false;
  bool flag_neg_taker = 0;
  int64_t ts_neg_taker = 0;

  std::vector<uint32_t> ids_neg_taker;
  std::string dist_symbol;

  std::vector<SubTopic> subs;
  std::vector<int64_t> sub_count;
  data::pnl_data pnl_data;

  const AccountPosition *account_position_;
  nova::trade::TradeEngine *engine_aim = nullptr;
  FairPriceGenerator fp_generator_; // 使用 FairPriceGenerator 类的实例
  OrderProcessor order_processor_;

private:
  data::InstrumentData InstData_;

  std::unordered_map<data::currency, data::fair_price_data> fps_map_;
  std::unordered_map<data::currency, data::BalanceManager> BlcMng_;
  std::unordered_map<data::currency, data::volume_data> volumes_;
  std::unordered_map<data::currency, data::hedge_position_data>
      hedge_positions_;
  std::unordered_map<data::currency, data::fetch_orders_data> fetch_orders_map_;

  std::unordered_map<data::UniInstID, std::vector<const StrategyApi::OrderTp *>>
      batch_orders;
  std::unordered_map<data::UniInstID, std::vector<data::BatchOrderParam>>
      batch_order_params;

private:
  std::ofstream file_cache_;
  std::ofstream monitor_file_cache_;

  data::PairStats pair_stats_;

  int n_threads_ = 1;
  std::atomic<bool> continue_{true};

  struct NOVA_ALIGNED_CACHE_LINE th_info {
    std::atomic<bool> completed{true};
    std::thread thread;
  };

  struct NOVA_ALIGNED_CACHE_LINE order_data {
    data::fetch_orders_data data;
  };

  std::vector<order_data> orders_data_;

  static constexpr auto MAX_THREADS = 32;
  NOVA_ALIGNED_CACHE_LINE th_info threads_[MAX_THREADS];
};
