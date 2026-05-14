/*****************************************************************************
 * Copyright (c) 2020 mengxi investment.
 * All rights reserved.
 *
 * Created by Yurong Chen (gghamasker@gmail.com) on 2025-12-22.
 *****************************************************************************/
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "taking_all_multi.h"
#include "trade/trade_server.h"

STRATEGY_API_IMPLEMENT(TakingDemo)
/************************************init*****************************************/
bool TakingDemo::on_init(const Config *cfg) {
  auto ts = sum_util::GetCurrentBeijingTimestampNs();
  /*
  ! Load Setting Configs
  */
  LoadStrategyConfig(cfg, CFG_);
  // ********* Start config auto-reload for log level *********
  config_reloader_ = std::make_unique<nova::config::ConfigReloader>();
  if (!config_reloader_->StartAutoReload(CFG_.config_path)) {
    WARNING_FLOG("[OnInit][{}]Failed to start config auto-reload, log level "
                 "changes will "
                 "not be applied automatically",
                 ts);
  } else {
    INFO_FLOG("[OnInit][{}]Config auto-reload started successfully for: {}", ts,
              CFG_.config_path);
  }
  // ********* Switch to real log level after initialization *********
  if (!CFG_.Server.Log.file_level_real.empty()) {
    // 直接传入已获取的file_level_real值
    if (config_reloader_) {
      config_reloader_->ManualReloadWithRealLevel(
          CFG_.Server.Log.file_level_real);
      INFO_FLOG(
          "[OnInit][{}]Switched to real log level: {} after initialization", ts,
          CFG_.Server.Log.file_level_real);

      // 记录日志级别设置信息
      INFO_FLOG("[OnInit][{}]Log level configuration:", ts);
      INFO_FLOG("[OnInit][{}]  - Global log level set to: {}", ts,
                CFG_.Server.Log.file_level_real);
      INFO_FLOG("[OnInit][{}]  - All threads will use this level automatically",
                ts);
    }
  } else {
    WARNING_FLOG("[OnInit][{}]Server.Log.file_level_real is empty, using "
                 "default log level",
                 ts);
  }
  /*
  ! Initialize instruments
  */
  get_trade_engine();
  INFO_FLOG("[OnInit][{}]Got {} swap instruments from config", ts,
            CFG_.instruments_swap.size());
  if (CFG_.instruments_swap.empty())
    throw std::runtime_error("Invalid instruments_swap config!");
  for (auto &item : CFG_.instruments_swap) {
    auto inst_swap_str = item.get<std::string>();
    subscribe_instruments(inst_swap_str, false);
  }
  INFO_FLOG("[OnInit][{}]Got {} spot instruments from config", ts,
            CFG_.instruments.size());
  if (CFG_.instruments.empty())
    throw std::runtime_error("Invalid instruments_spot config!");
  for (auto &item : CFG_.instruments) {
    auto inst_str = item.get<std::string>();
    subscribe_instruments(inst_str, true);
  }
  /*
  ! Init Variables
  */
  init_global_variables();
  /*
  ! Read balances / positions
  */
  get_assets();
  for (auto &c : CFG_.trading_currencies) {
    const double &balance_setting = CFG_.setting.at(c).balance_quantity;
    BlcMng_[c].balance_default = balance_setting;
    INFO_FLOG("[OnInit][{}]Initialized coin {} balances_default: {}", ts,
              data::get_currency_name(c), balance_setting);
    // 获取实际仓位 prod从engine获取 其余从Setting.yml
    if (CFG_.flag_prod)
      BlcMng_[c].balance_live = BlcMng_[c].balance_query;
    else if (CFG_.customer_balance)
      BlcMng_[c].balance_live = CFG_.setting.at(c).customer_balance;
    else
      BlcMng_[c].balance_live = balance_setting;
    INFO_FLOG("[OnInit][{}]Fetched {} balance_init: {}", ts,
              data::get_currency_name(c), BlcMng_[c].balance_live);
  }
  if (CFG_.flag_prod && CFG_.adjust_usd != -1) {
    INFO_FLOG("[OnInit][{}]Not used whole USD, set to {} from {}", ts,
              CFG_.adjust_usd, BlcMng_[data::currency::USD].balance_live);
    BlcMng_[data::currency::USD].balance_live = CFG_.adjust_usd;
  }
  /*
  ! Subscribe instruments
  */
  if (!(last_data_info = this->SubDataInfo(subs))) {
    ERROR_LOG("[OnInit]Sub quote failed");
    return false;
  }
  INFO_LOG("[OnInit]Sub quote success");
  return true;
}

void TakingDemo::on_stop() { INFO_FLOG("[OnStop]Stopping strategy"); }

void TakingDemo::on_datainfo(const DataInfoManager *datainfo, int32_t di,
                             const SecurityPosition *position) {}

void TakingDemo::on_reminder(void *data, uint64_t cur_ns) {}

void TakingDemo::get_trade_engine() {
  if (engine_aim == nullptr) {
    auto sym = "BTC_NONE";
    auto exch = GetExchangeFromStr(CFG_.aim_exchange.c_str());
    const InstrumentId id_aim = InstrumentId::Create(sym, exch);
    auto *server = GetServer();
    engine_aim =
        dynamic_cast<TradeServer *>(server)->service()->EngineByInstrument(
            id_aim);
    assert(engine_aim != nullptr && "Invalid trade engine");
  }
}

void TakingDemo::init_global_variables() {
  vol_multi_tmp = CFG_.vol_multi;
  for (const auto &c : CFG_.trading_currencies) {
    BlcMng_.emplace(c, data::BalanceManager());
    fps_map_.emplace(c, data::fair_price_data());
    volumes_.emplace(c, data::volume_data());
  }
  for (auto &forex : constant::FOREX) {
    const auto &ib_inst = fmt::format("{}_usd_cash.idealpro", forex);
    const auto &id_ib_inst = InstData_.IM.inststr2id_[ib_inst];
    if (sum_util::Find(InstData_.bbo_map, id_ib_inst))
      CFG_.available_forex.push_back(forex);
  }
  for (auto &item : InstData_.depth_map) {
    auto &id = item.first;
    auto *IC = InstData_.IM.FindByUniId(id);
    if (IC->flag_trading)
      InstData_.trading_ids.push_back(id);
  }
  INFO_FLOG("Initialized global BlcMng_ / fps_map_ / volumes_ / "
            "InstData.trading_ids / CFG_available_forex variables");
}

void TakingDemo::get_assets() {
  if (CFG_.flag_prod) { // * 从交易所或者回测从Setting读取资产
    auto *fund_assets_aim = GetFundAsset(engine_aim);
    INFO_FLOG("[GetAssets]Got balances({}) from {} exchange:",
              fund_assets_aim->size(), CFG_.aim_exchange);
    for (const auto &entry : *fund_assets_aim)
      INFO_FLOG("[GetAssets]symbol: {}, available: {}, hold: {}",
                entry.first.c_str(), entry.second->total_asset,
                entry.second->fund_frozen);
    for (auto &c : CFG_.trading_currencies) { // ! 从trade_engine获取资产
      // 需要用与交易所匹配的currency symbol
      auto currency_str = data::get_currency_name(c);
      // ! 每次加新currency时 需要到prod查看获取的symbol名是否一致
      const auto &currency2 = DataProcess::format_main_exchange_none_pair(
          currency_str, CFG_.aim_exchange);
      auto &balance_query = BlcMng_.at(c).balance_query;
      auto &balance_live = BlcMng_.at(c).balance_live;
      auto &balance_default = BlcMng_.at(c).balance_default;
      if (fund_assets_aim->find(currency2) == fund_assets_aim->end())
        balance_query = 0;
      else
        balance_query = fund_assets_aim->at(currency2)->total_asset;
      if (std::abs(balance_query - balance_live) > balance_default * 0.01)
        ERROR_FLOG("[GetAssets]{} recorded balance not match query balance! "
                   "balance_query: {}, balance_live: {}, balance_default: {}",
                   currency2, balance_query, balance_live, balance_default);
    }
  }
}

void TakingDemo::subscribe_instruments(std::string inst_str, bool sub_spot) {
  // ! 1. Set init variables
  if (!CFG_.flag_ib && sum_util::EndsWith(inst_str, "idealpro"))
    return;
  INFO_FLOG("[SubInst]inst: {}", inst_str);
  auto type_str = sub_spot ? "spot" : "swap";
  auto inst_id = InstrumentId::Create(inst_str);
  inst_str = inst_id.GetSymbol();
  auto exch_str = GetExchangeStrFromId(inst_id.exchange);
  if (inst_id.Valid() == false) {
    ERROR_FLOG("[SubInst]create {} inst {} error!", type_str, inst_str);
    return;
  }
  auto *base_info =
      sum_util::StrEqual(exch_str, "idealpro") ? nullptr : GetBaseInfo(inst_id);
  auto IC_tmp = data::InstrumentComponent{inst_id, base_info,
                                          sub_spot ? CFG_.prob_params_dir : ""};
  if (InstData_.IM.Exists(IC_tmp.uni_id)) {
    ERROR_FLOG("[SubInst]{} inst {} repeat!", type_str, inst_str);
    return;
  }
  INFO_FLOG("[SubInst]IC initialized: {} {} {} {}", IC_tmp.uni_id,
            IC_tmp.inst_str, IC_tmp.base_str, IC_tmp.quote_str);
  InstData_.IM.Insert(IC_tmp);
  auto *IC = InstData_.IM.FindByUniId(IC_tmp.uni_id);
  // ! 2. Initialize delay_map for this exchange+inst_type if not exists
  auto delay_key =
      data::delay_data::make_key(inst_id.exchange, inst_id.inst_type);
  if (InstData_.delay_map.find(delay_key) == InstData_.delay_map.end()) {
    InstData_.delay_map[delay_key] = data::delay_data();
    InstData_.delay_map[delay_key].verbose = CFG_.verbose_delay;
    INFO_FLOG("[SubInst]Initialized delay_map for exchange: {}, inst_type: {}",
              exch_str, GetCoinInstTypeString(inst_id.inst_type));
  }
  // ! 3. Init hedge positions
  auto *posi = CreateSecurityPosition(inst_id);
  if (sub_spot) {
    ChangePositionToSingleSide(posi);
  } else {
    // swap需要获取long/short仓位来更新posi
    auto *acc_pos = this->GetAccountPosition(inst_id);
    account_position_ = acc_pos;
    if (CFG_.flag_prod) {
      posi->long_position = acc_pos->long_position;
      posi->short_position = acc_pos->short_position;
      hedge_positions_[IC->base].size =
          account_position_->short_position; // todo: 针对bn swap
    } else {
      const auto &base = IC->base;
      posi->long_position = acc_pos->long_position;
      posi->short_position = CFG_.setting.at(base).balance_quantity;
      hedge_positions_[IC->base].size = posi->short_position;
    }
    // InstData_.order_map.emplace(IC->uni_id, data::OrderManager{inst_id,
    // posi});
    if (DataProcess::is_main_exchange(inst_str, "bn"))
      InstData_.swap_order_map.emplace(IC->uni_id,
                                       data::OrderManager{inst_id, posi});
  }
  // ! 4. Init components for aim/other exchanges
  // * initilize trade_map / depth_map
  InstData_.trade_map[IC_tmp.uni_id] = data::trades_data{};
  subs.emplace_back(SubTopic{posi, NOVA_COIN_QUOTE_TRADE, true});
  INFO_FLOG("[SubInst]{} {} subscribed Trade (id: {})", type_str, inst_str,
            IC->uni_id);
  NOVA_COIN_QUOTE_TYPE depth_type =
      (sum_util::EndsWith(inst_str, CFG_.aim_exchange))
          ? NOVA_COIN_QUOTE_DEPTH_LVN
          : NOVA_COIN_QUOTE_DEPTH;
  InstData_.depth_map[IC_tmp.uni_id] = data::depths_data();
  InstData_.depth_map[IC_tmp.uni_id].taker_fee =
      DataProcess::get_exchange_taker_fee(CFG_, exch_str);
  InstData_.depth_map[IC_tmp.uni_id].tick_size = IC->tick_size;
  InstData_.depth_map[IC_tmp.uni_id].verbose = CFG_.verbose_ob;
  InstData_.depth_map[IC_tmp.uni_id].PC =
      GetPairConfig(CFG_.pair_configs, inst_str);
  const auto &PC = InstData_.depth_map[IC_tmp.uni_id].PC;
  INFO_FLOG("{} get pair config ema_alpha: {}, wp_invalid_ticks: {}, "
            "depth5_summary_percentile_bid: {}, depth5_summary_percentile_ask: "
            "{}, depth5_thr_bid: {}, depth5_thr_ask: {}",
            inst_str, PC.ema_alpha, PC.wp_invalid_ticks,
            PC.depth5_summary_percentile_bid, PC.depth5_summary_percentile_ask,
            PC.depth5_thr_bid, PC.depth5_thr_ask);
  subs.emplace_back(SubTopic{posi, depth_type, true});
  INFO_FLOG("[SubInst]{} {} subscribed Depth/DepthLVN (id: {})", type_str,
            inst_str, IC->uni_id);
  if (DataProcess::is_main_exchange(inst_str, CFG_.aim_exchange)) {
    // * initilize rate_limit / vol_map / order_map
    if (CFG_.flag_prod)
      IC->rate_limit = engine_aim->GetRateLimit(inst_str);
    else
      IC->rate_limit = 225;
    INFO_FLOG("[SubInst]{} {} taker_fee: {}, rate_limit: {}", type_str,
              inst_str, InstData_.depth_map[IC->uni_id].taker_fee,
              IC->rate_limit);
    InstData_.vol_map[IC->uni_id] = data::vol_data();
    InstData_.vol_map[IC->uni_id].slope.factor_max = CFG_.factor_max;
    InstData_.vol_map[IC->uni_id].slope.factor_min = CFG_.factor_min;
    InstData_.vol_map[IC->uni_id].slope.k = -std::log(
        1.0 - (1.0 - CFG_.factor_min) / (CFG_.factor_max - CFG_.factor_min));
    InstData_.insert_order_map.emplace(IC->uni_id, data::insert_orders_data());
    InstData_.order_map.emplace(IC->uni_id, data::OrderManager{inst_id, posi});
    // 就地构造，避免在栈上创建约 10MB 的临时对象导致栈溢出
    pair_stats_.per_inst.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(IC->uni_id),
                                 std::forward_as_tuple());
  } else {
    InstData_.bbo_map[IC->uni_id] = data::bbo_data();
    InstData_.bbo_map[IC->uni_id].tick_size = IC->tick_size;
    InstData_.bbo_map[IC->uni_id].taker_fee =
        DataProcess::get_exchange_taker_fee(CFG_, exch_str);
    InstData_.bbo_map[IC->uni_id].PC =
        GetPairConfig(CFG_.pair_configs, inst_str);
    subs.emplace_back(SubTopic{posi, NOVA_COIN_QUOTE_BBO, true});
    INFO_FLOG("[SubInst]{} {} subscribed BBO (id: {})", type_str, inst_str,
              IC->uni_id);
  }
  INFO_FLOG("[SubInst]After subscribed, depth_map size: {}, bbo_map size: {}, "
            "vol_map size: {}, trade_map size: {}, order_map size: {}, "
            "swap_order_map size: {}",
            InstData_.depth_map.size(), InstData_.bbo_map.size(),
            InstData_.vol_map.size(), InstData_.trade_map.size(),
            InstData_.order_map.size(), InstData_.swap_order_map.size());
}

/******************************** quote *********************************/
void TakingDemo::on_depth(const Depth *md, const SecurityPosition *position) {
  (void)position;
  last_local_ns = md->local_ns;
  // position->short_position;
  // process_quote(md->instrument_id, md, position);
}

void TakingDemo::on_trade(const Trade *trade,
                          const SecurityPosition *position) {
  (void)position;
  last_local_ns = trade->local_ns;
  // position->short_position;
  // process_quote(trade->instrument_id, nullptr, position);
}

void TakingDemo::on_bbo(const BBO *bbo, const SecurityPosition *position) {
  (void)position;
  last_local_ns = bbo->local_ns;
  // position->short_position;
  // process_quote(bbo->instrument_id, bbo, position);
}

/****************************** order & trade******************************/
void TakingDemo::on_order_update(const StrategyApi::OrderTp *tp,
                                 const SecurityPosition *position) {}

void TakingDemo::on_order_cancelled(const StrategyApi::OrderTp *tp,
                                    const SecurityPosition *position,
                                    double qty_cancelled) {}

void TakingDemo::on_order_cancel_failed(const OrderTp *tp,
                                        const SecurityPosition *position,
                                        int32_t reason) {}

void TakingDemo::on_order_rejected(const StrategyApi::OrderTp *order,
                                   const SecurityPosition *position,
                                   int32_t reason) {}

void TakingDemo::on_order_done(const StrategyApi::OrderTp *tp,
                               const SecurityPosition *position,
                               NOVA_ORDER_STATUS done_status) {}

void TakingDemo::on_order_accepted(const StrategyApi::OrderTp *order,
                                   const SecurityPosition *position) {}

void TakingDemo::on_order_amended(const OrderTp *order,
                                  const SecurityPosition *position,
                                  int32_t reason) {}

void TakingDemo::OnRspTradeInformation(TradeEngine *engine, nlohmann::json &rsp,
                                       int req_id) {}