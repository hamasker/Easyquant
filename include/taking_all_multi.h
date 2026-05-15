#pragma once

#include "common/data.h"
#include "config_reloader.h"
#include "configs/configs.h"
#include "data_process.h"
#include "nova_trader_api.h"

USE_NOVA_NAMESPACE(base)
USE_NOVA_NAMESPACE(quote)
USE_NOVA_NAMESPACE(trade)

class TakingDemo : public StrategyApi {

public: // * 构造/析构函数
  TakingDemo() : StrategyApi(), account_position_(nullptr) {}
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

  const DataInfoManager *last_data_info;

  Configs CFG_;
  std::unique_ptr<nova::config::ConfigReloader> config_reloader_;

private:
  std::vector<SubTopic> subs;
  const AccountPosition *account_position_;
  nova::trade::TradeEngine *engine_aim = nullptr;

private:
  data::InstrumentData InstData_;

  std::unordered_map<data::currency, data::fair_price_data> fps_map_;
  std::unordered_map<data::currency, data::BalanceManager> BlcMng_;
  std::unordered_map<data::currency, data::volume_data> volumes_;
  std::unordered_map<data::currency, data::hedge_position_data>
      hedge_positions_;

private:
  data::PairStats pair_stats_;
};
