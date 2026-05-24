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
  /*
  ! Load Configs
  */
  auto ts = sum_util::GetCurrentBeijingTimestampNs();
  LoadConfigs(cfg, CFG_);
  INFO_FLOG(
      "[OnInit]path: {}, root_path: {}, config_file_path: {}, "
      "pair_configs_path: {}, prob_params_dir: {}, balance_params_path: {}",
      cfg->config_file(), CFG_.Strategy.Stable.root_dir,
      CFG_.Strategy.Stable.config_file_path,
      CFG_.Strategy.Stable.pair_configs_path,
      CFG_.Strategy.Stable.prob_params_dir,
      CFG_.Strategy.Stable.balance_params_path);
  // ********* Config auto-reload for log level *********
  config_reloader_ = std::make_unique<nova::config::ConfigReloader>();
  if (!config_reloader_->StartAutoReload(
          CFG_.Strategy.Stable.config_file_path)) {
    WARNING_FLOG("[OnInit][{}]Failed to start config auto-reload", ts);
  } else {
    INFO_FLOG("[OnInit][{}]Config auto-reload started successfully for: {}", ts,
              CFG_.Strategy.Stable.config_file_path);
  }
  // ********* Switch to real log level after initialization *********
  if (!CFG_.Server.Log.file_level_real.empty()) {
    // 直接传入已获取的file_level_real值
    if (config_reloader_) {
      config_reloader_->ManualReloadWithRealLevel(
          CFG_.Server.Log.file_level_real);
      INFO_FLOG("[OnInit][{}]Switched to real log level: {}", ts,
                CFG_.Server.Log.file_level_real);
    }
  } else {
    WARNING_FLOG(
        "[OnInit][{}]Server.Log.file_level_real is empty, using default", ts);
  }
  /*
  ! Initialize Instruments
  */
  get_trade_engine();
  INFO_FLOG("[OnInit] subscribe {} swaps...",
            CFG_.Strategy.Stable.instruments_swap.size());
  if (CFG_.Strategy.Stable.instruments_swap.empty())
    throw std::runtime_error("Invalid instruments_swap config!");
  for (const auto &inst_swap_str : CFG_.Strategy.Stable.instruments_swap) {
    subscribe_instruments(inst_swap_str, false);
  }
  INFO_FLOG("[OnInit] subscribe {} spots...",
            CFG_.Strategy.Stable.instruments.size());
  if (CFG_.Strategy.Stable.instruments.empty())
    throw std::runtime_error("Invalid instruments_spot config!");
  for (const auto &inst_str : CFG_.Strategy.Stable.instruments) {
    subscribe_instruments(inst_str, true);
  }
  /*
  ! Initialize Variables
  */
  init_global_variables();
  /*
  ! Read Balances / Positions
  */
  get_assets();
  for (auto &c : CFG_.Strategy.Stable.trading_currencies) {
    const double &balance_setting =
        CFG_.Strategy.setting.at(c).balance_quantity;
    BlcMng_[c].balance_default = balance_setting;
    INFO_FLOG("[OnInit][{}]Initialized coin {} balances_default: {}", ts,
              data::get_currency_name(c), balance_setting);
    // 获取实际仓位 prod从engine获取 其余从Setting.yml
    if (CFG_.Strategy.flag_prod)
      BlcMng_[c].balance_live = BlcMng_[c].balance_query;
    else if (CFG_.Strategy.Stable.customer_balance)
      BlcMng_[c].balance_live = CFG_.Strategy.setting.at(c).customer_balance;
    else
      BlcMng_[c].balance_live = balance_setting;
    INFO_FLOG("[OnInit][{}]Fetched {} balance_init: {}", ts,
              data::get_currency_name(c), BlcMng_[c].balance_live);
  }
  if (CFG_.Strategy.flag_prod && CFG_.Strategy.Stable.customer_usd != -1) {
    INFO_FLOG("[OnInit][{}]Override USD balance from {} to {}", ts,
              BlcMng_[data::currency::USD].balance_live,
              CFG_.Strategy.Stable.customer_usd);
    BlcMng_[data::currency::USD].balance_live =
        CFG_.Strategy.Stable.customer_usd;
  }
  /*
  ! Subscribe Instruments
  */
  if (!(last_data_info = this->SubDataInfo(subs))) {
    ERROR_LOG("[OnInit]Sub quote failed");
    return false;
  }
  INFO_FLOG("[OnInit]Sub quote success");

  // 初始化 FP 引擎和订单引擎
  fpg_ = std::make_unique<FairPriceGenerator>(fps_map_, InstData_, CFG_);
  OP_ = std::make_unique<OrderProcessor>(InstData_, CFG_, fps_map_, BlcMng_,
                                         global_ts);
  OP_->Init();
  INFO_FLOG("[OnInit]FP generator and OrderProcessor initialized");

  scheduler_.init(CFG_.Strategy.Stable.negative_interval,
                  CFG_.Strategy.Stable.fp_turnover_usd,
                  CFG_.Strategy.Stable.order_turnover_usd,
                  CFG_.Strategy.Stable.fp_interval_max_ms,
                  CFG_.Strategy.Stable.order_interval_max_ms, ts);
  return true;
}

void TakingDemo::on_stop() { INFO_FLOG("[OnStop]Stopping strategy"); }

void TakingDemo::on_datainfo(const DataInfoManager *datainfo, int32_t di,
                             const SecurityPosition *position) {
  if (!datainfo) {
    ERROR_FLOG("[OnDatainfo]undefined datainfo");
    return;
  }
  /*
  ! 读取检查数据
  */
  if (CFG_.Strategy.Verbose.ob && scheduler_.flag_first)
    DEBUG_FLOG("[VerboseOb] before fetch data, depth_map: {} bbo_map: {} "
               "trade_map: {}",
               InstData_.depth_map.size(), InstData_.bbo_map.size(),
               InstData_.trade_map.size());
  const auto &one = datainfo->datainfo().at(di);
  scheduler_.new_data_count_++;
  scheduler_.flag_new_data = true;
  auto qt = (int)one.quote_type();
  if (CFG_.Strategy.Verbose.ob)
    DEBUG_FLOG("[VerboseOb] on_datainfo di={} qtype={}", di, qt);
  scheduler_.flag_data_ready =
      DataProcess::fetch_data(one, InstData_, scheduler_.flag_data_ready, CFG_);
  // 更新 global_ts
  auto quote_type = one.quote_type();
  if (quote_type == NOVA_COIN_QUOTE_BBO) {
    const auto &data = *static_cast<const BBO *>(one.buffer().back());
    global_ts = data.local_time;
  } else if (quote_type == NOVA_COIN_QUOTE_BAR) {
    const auto &data = *static_cast<const Bar *>(one.buffer().back());
    global_ts = data.local_time;
  } else if (quote_type == NOVA_COIN_QUOTE_DEPTH) {

    const auto &data = *static_cast<const Depth *>(one.buffer().back());
    global_ts = data.local_time;
  } else if (quote_type == NOVA_COIN_QUOTE_DEPTH_LVN) {

    const auto &data = *static_cast<const DepthLVN *>(one.buffer().back());
    global_ts = data.local_time;
  } else if (quote_type == NOVA_COIN_QUOTE_TRADE) {
    const auto *trade = static_cast<const Trade *>(one.buffer().back());
    global_ts = trade->local_time;
    // turnover 累加
    auto it = InstData_.IM.inststr2id_.find(trade->instrument_id.symbol);
    if (it != InstData_.IM.inststr2id_.end()) {
      auto *IC = InstData_.IM.FindByUniId(it->second);
      if (IC)
        accumulate_turnover(*IC, trade->price, trade->qty);
    }
  }
  if (scheduler_.flag_first) {
    INFO_FLOG("[OnStrategy]trigger on_datainfo success {}",
              position->instrument.symbol);
    scheduler_.flag_first = false;
    AddReminder(global_ts + 10'000'000, nullptr);
    // 在第一次数据触发时再次设置日志级别, 确保新创建的线程都使用统一级别
    if (!CFG_.Server.Log.file_level_real.empty() && config_reloader_) {
      config_reloader_->ManualReloadWithRealLevel(
          CFG_.Server.Log.file_level_real);
      INFO_FLOG("Re-applied log level {} at first data trigger",
                CFG_.Server.Log.file_level_real);
    }
  }
}

void TakingDemo::on_reminder(void *, uint64_t cur_ns) {
  do_calculations(static_cast<int64_t>(cur_ns));
  AddReminder(cur_ns + 10'000'000, nullptr); // 10ms 后再次触发
}

void TakingDemo::on_poll(int64_t /*ts*/) {
  if (scheduler_.new_data_count_ > 0) {
    scheduler_.new_data_count_ = 0;
    do_calculations(global_ts);
  }
}

void TakingDemo::do_calculations(int64_t current_ts) {
  // * 检测数据完备
  if (!scheduler_.flag_data_ready)
    return;
  InstData_.global_ts = current_ts;
  // * 检测断连
  if (scheduler_.should_disconnect(current_ts)) {
    process_disconnect(current_ts);
    scheduler_.mark_disconnect(current_ts);
    return;
  }
  // * 撤负收益
  if (scheduler_.should_negative(current_ts)) {
    process_negative(current_ts);
    scheduler_.mark_negative(current_ts);
    return;
  }
  // * 计算fair price
  if (scheduler_.should_fp(current_ts)) {
    process_fp(current_ts);
    scheduler_.mark_fp(current_ts);
  }
  // * 生成订单
  if (scheduler_.should_order(current_ts)) {
    process_order(current_ts);
    scheduler_.mark_order(current_ts);
  }
}

void TakingDemo::process_disconnect(int64_t ts) {
  // 断连处理: 撤所有挂单
  INFO_FLOG("[Strategy] disconnect detected, cancelling all orders");
}

void TakingDemo::process_negative(int64_t ts) {
  // 负收益订单处理: 检查和撤销
}

void TakingDemo::process_fp(int64_t ts) {
  // 拉取最新数据 → 计算 FP → 异常检测
  DataProcess::fetch_data_all(last_data_info, InstData_, CFG_);
  if (!fpg_->update(ts))
    return;
}

void TakingDemo::process_order(int64_t ts) {
  // 预处理 → 下单
}

void TakingDemo::accumulate_turnover(const data::InstrumentComponent &IC,
                                     double price, double qty) {
  // 全市场成交额驱动 FP 频率 — Binance 量大触发快, 不依赖单一交易所
  const auto &q = IC.quote_str;
  if (q != "usd" && q != "usdt" && q != "usdc")
    return;
  scheduler_.add_turnover(price * qty);
}

void TakingDemo::get_trade_engine() {
  if (engine_aim == nullptr) {
    auto sym = "BTC_NONE";
    auto exch = GetExchangeFromStr(CFG_.Strategy.Stable.aim_exchange.c_str());
    const InstrumentId id_aim = InstrumentId::Create(sym, exch);
    auto *server = GetServer();
    engine_aim =
        dynamic_cast<TradeServer *>(server)->service()->EngineByInstrument(
            id_aim);
    assert(engine_aim != nullptr && "Invalid trade engine");
  }
}

void TakingDemo::init_global_variables() {
  for (const auto &c : CFG_.Strategy.Stable.trading_currencies) {
    BlcMng_.emplace(c, data::BalanceManager());
    fps_map_.emplace(c, data::fair_price_data());
    volumes_.emplace(c, data::volume_data());
  }
  for (auto &forex : constant::FOREX) {
    const auto &Cusd_inst = DataProcess::format_main_exchange_usd_pair(
        forex, CFG_.Strategy.Stable.aim_exchange);
    const auto &id_Cusd_inst = InstData_.IM.inststr2id_[Cusd_inst];
    if (sum_util::Find(InstData_.depth_map, id_Cusd_inst))
      CFG_.Strategy.available_forex.push_back(forex);
  }
  INFO_FLOG("Initialized available forex: {}",
            sum_util::ToString(CFG_.Strategy.available_forex));
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
  if (CFG_.Strategy.flag_prod) { // * 从交易所或者回测从Setting读取资产
    auto *fund_assets_aim = GetFundAsset(engine_aim);
    INFO_FLOG("[GetAssets]Got balances({}) from {} exchange:",
              fund_assets_aim->size(), CFG_.Strategy.Stable.aim_exchange);
    for (const auto &entry : *fund_assets_aim)
      INFO_FLOG("[GetAssets]symbol: {}, available: {}, hold: {}",
                entry.first.c_str(), entry.second->total_asset,
                entry.second->fund_frozen);
    for (auto &c :
         CFG_.Strategy.Stable.trading_currencies) { // ! 从trade_engine获取资产
      // 需要用与交易所匹配的currency symbol
      auto currency_str = data::get_currency_name(c);
      // ! 每次加新currency时 需要到prod查看获取的symbol名是否一致
      const auto &currency2 = DataProcess::format_main_exchange_none_pair(
          currency_str, CFG_.Strategy.Stable.aim_exchange);
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
  if (!CFG_.Strategy.flag_ib && sum_util::EndsWith(inst_str, "idealpro")) {
    INFO_FLOG("[SubInst] {} skip (idealpro)", inst_str);
    return;
  }
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
  auto IC_tmp = data::InstrumentComponent{
      inst_id, base_info, sub_spot ? CFG_.Strategy.Stable.prob_params_dir : ""};
  if (InstData_.IM.Exists(IC_tmp.uni_id)) {
    ERROR_FLOG("[SubInst]{} inst {} repeat!", type_str, inst_str);
    return;
  }
  InstData_.IM.Insert(IC_tmp);
  auto *IC = InstData_.IM.FindByUniId(IC_tmp.uni_id);
  auto delay_key =
      data::delay_data::make_key(inst_id.exchange, inst_id.inst_type);
  if (InstData_.delay_map.find(delay_key) == InstData_.delay_map.end()) {
    InstData_.delay_map[delay_key] = data::delay_data();
    InstData_.delay_map[delay_key].verbose = CFG_.Strategy.Verbose.delay;
  }
  auto *posi = CreateSecurityPosition(inst_id);
  if (sub_spot) {
    ChangePositionToSingleSide(posi);
  } else {
    auto *acc_pos = this->GetAccountPosition(inst_id);
    account_position_ = acc_pos;
    if (CFG_.Strategy.flag_prod) {
      posi->long_position = acc_pos->long_position;
      posi->short_position = acc_pos->short_position;
      hedge_positions_[IC->base].size =
          account_position_->short_position; // todo: 针对bn swap
    } else {
      const auto &base = IC->base;
      posi->long_position = acc_pos->long_position;
      auto it = CFG_.Strategy.setting.find(base);
      if (it == CFG_.Strategy.setting.end()) {
        posi->short_position = 0;
      } else {
        posi->short_position = it->second.balance_quantity;
      }
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
      (sum_util::EndsWith(inst_str, CFG_.Strategy.Stable.aim_exchange))
          ? NOVA_COIN_QUOTE_DEPTH_LVN
          : NOVA_COIN_QUOTE_DEPTH;
  InstData_.depth_map[IC_tmp.uni_id] = data::depths_data();
  InstData_.depth_map[IC_tmp.uni_id].taker_fee =
      DataProcess::get_exchange_taker_fee(CFG_, exch_str);
  InstData_.depth_map[IC_tmp.uni_id].tick_size = IC->tick_size;
  InstData_.depth_map[IC_tmp.uni_id].verbose = CFG_.Strategy.Verbose.ob;
  InstData_.depth_map[IC_tmp.uni_id].PC =
      GetPairConfig(CFG_.Strategy.pair_configs, inst_str);
  const auto &PC = InstData_.depth_map[IC_tmp.uni_id].PC;
  INFO_FLOG("{} get pair config ema_alpha: {}, wp_invalid_ticks: {}, "
            "depth5_summary_percentile_bid: {}, depth5_summary_percentile_ask: "
            "{}, depth5_thr_bid: {}, depth5_thr_ask: {}",
            inst_str, PC.ema_alpha, PC.wp_invalid_ticks,
            PC.depth5_summary_percentile_bid, PC.depth5_summary_percentile_ask,
            PC.depth5_thr_bid, PC.depth5_thr_ask);
  subs.emplace_back(SubTopic{posi, depth_type, true});
  INFO_FLOG("[SubInst]{} {} subscribed {} (id: {})", type_str, inst_str,
            depth_type == NOVA_COIN_QUOTE_DEPTH_LVN ? "DepthLVN" : "Depth",
            IC->uni_id);
  if (DataProcess::is_main_exchange(inst_str,
                                    CFG_.Strategy.Stable.aim_exchange)) {
    // * initilize rate_limit / vol_map / order_map
    if (CFG_.Strategy.flag_prod)
      IC->rate_limit = engine_aim->GetRateLimit(inst_str);
    else
      IC->rate_limit = 225;
    InstData_.vol_map[IC->uni_id] = data::vol_data();
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
        GetPairConfig(CFG_.Strategy.pair_configs, inst_str);
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
  INFO_FLOG("[SubInst] {} {} done", inst_str, sub_spot ? "spot" : "swap");
}

/******************************** quote *********************************/
void TakingDemo::on_depth(const Depth *, const SecurityPosition *) {}

void TakingDemo::on_trade(const Trade *, const SecurityPosition *) {}

void TakingDemo::on_bbo(const BBO *, const SecurityPosition *) {}

/****************************** order & trade******************************/
void TakingDemo::on_order_update(const StrategyApi::OrderTp *order,
                                 const SecurityPosition *) {
  INFO_FLOG("[Strategy] on_order_update: qty={} filled={} left={}",
            order ? order->order.quantity : 0, order ? order->qty_traded : 0,
            order ? order->qty_left : 0);
}

void TakingDemo::on_order_cancelled(const StrategyApi::OrderTp *order,
                                    const SecurityPosition *, double left) {
  INFO_FLOG("[Strategy] on_order_cancelled: qty={} left={}",
            order ? order->order.quantity : 0, left);
}

void TakingDemo::on_order_cancel_failed(const StrategyApi::OrderTp *order,
                                        const SecurityPosition *,
                                        int32_t reason) {
  INFO_FLOG("[Strategy] on_order_cancel_failed: id={} reason={}",
            order ? order->nova_id.sequence : 0, reason);
}

void TakingDemo::on_order_rejected(const StrategyApi::OrderTp *order,
                                   const SecurityPosition *, int32_t reason) {
  INFO_FLOG("[Strategy] on_order_rejected: qty={} price={} reason={}",
            order ? order->order.quantity : 0, order ? order->order.price : 0,
            reason);
}

void TakingDemo::on_order_done(const StrategyApi::OrderTp *order,
                               const SecurityPosition *, NOVA_ORDER_STATUS st) {
  INFO_FLOG("[Strategy] on_order_done: qty={} qty_traded={} status={}",
            order ? order->order.quantity : 0, order ? order->qty_traded : 0,
            (int)st);
}

void TakingDemo::on_order_accepted(const StrategyApi::OrderTp *order,
                                   const SecurityPosition *) {
  INFO_FLOG("[Strategy] on_order_accepted: qty={} price={}",
            order ? order->order.quantity : 0, order ? order->order.price : 0);
}

void TakingDemo::on_order_amended(const StrategyApi::OrderTp *,
                                  const SecurityPosition *, int32_t) {}