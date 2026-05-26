/*****************************************************************************
 * Copyright (c) 2020 mengxi investment.
 * All rights reserved.
 *
 * Created by Yurong Chen (gghamasker@gmail.com) on 2025-12-22.
 *****************************************************************************/
#include "container_util.h"
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "common/volume_pairs.h"
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

  // ── turnover 订阅: 先收策略标的, 再补 top-N 缺口 ──
  // 1. 策略已订阅的 spot (含 trade 频道)
  for (auto &item : InstData_.trade_map) {
    auto symbol = InstData_.IM.FindByUniId(item.first)->inst_str;
    turnover_pairs_.insert(symbol);
  }

  // 2. top-N 中未订阅的补上 (只订 trade)
  for (auto &pair : fetch_all_top_pairs(20)) {
    auto inst_id = InstrumentId::Create(pair);
    if (!inst_id.Valid())
      continue;
    auto *base_info = GetBaseInfo(inst_id);
    auto IC_tmp = data::InstrumentComponent{inst_id, base_info, ""};
    InstData_.IM.Insert(IC_tmp);
    auto *posi = CreateSecurityPosition(inst_id);
    subs.emplace_back(SubTopic{posi, NOVA_COIN_QUOTE_TRADE, true});
    turnover_pairs_.insert(pair);
  }
  INFO_FLOG("[OnInit] turnover subs: {}", turnover_pairs_.size());
  INFO_FLOG("[OnInit] turnover subs: {}", sum_util::ToString(turnover_pairs_));
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
  INFO_FLOG("[OnInit]FairPriceGenerator and OrderProcessor initialized");

  // 加载 drift 表 (按 base 币种)
  for (auto &c : CFG_.Strategy.Stable.trading_currencies) {
    std::string base = data::get_currency_name(c);
    if (base == "btc")
      base = "xbt";
    std::string drift_path = CFG_.Strategy.Stable.root_dir +
                             "python/drift_tables/drift_" + base + "usd.csv";
    try {
      DriftTable dt;
      if (dt.load(drift_path))
        drift_tables_[base] = std::move(dt);
      INFO_FLOG("[OnInit]Drift table {}: {}", base,
                dt.empty() ? "not found" : "loaded");
    } catch (const std::exception &e) {
      WARNING_FLOG("[OnInit]Drift table load error {}: {}", base, e.what());
    }
  }

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
  if (scheduler_.flag_first)
    DEBUG_FLOG("[VerboseOb] before fetch data, depth_map: {} bbo_map: {} "
               "trade_map: {}, turnover_pairs: {}",
               InstData_.depth_map.size(), InstData_.bbo_map.size(),
               InstData_.trade_map.size(), turnover_pairs_.size());
  const auto &one = datainfo->datainfo().at(di);
  scheduler_.new_data_count_++;
  scheduler_.flag_new_data = true;
  auto quote_type = one.quote_type();
  // if (CFG_.Strategy.Verbose.ob)
  //   DEBUG_FLOG("[VerboseOb] on_datainfo di={} qtype={}", di, quote_type);
  // if (quote_type == NOVA_COIN_QUOTE_TRADE && ) {
  scheduler_.flag_data_ready =
      DataProcess::fetch_data(one, InstData_, scheduler_.flag_data_ready, CFG_);
  // 更新 global_ts
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
    // turnover 累加 (O(1) 集合判断)
    INFO_FLOG("trade_symbol: {}, price: {}, qty: {}",
              trade->instrument_id.symbol, trade->price, trade->qty);
    if (turnover_pairs_.find(trade->instrument_id.symbol) !=
        turnover_pairs_.end())
      scheduler_.add_turnover(trade->price * trade->qty);
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

void TakingDemo::process_disconnect(int64_t /*ts*/) {
  INFO_FLOG("[Strategy] disconnect detected, cancelling all orders");
  for (auto &id : InstData_.trading_ids) {
    auto it = InstData_.order_map.find(id);
    if (it == InstData_.order_map.end())
      continue;
    for (auto *order : it->second.left_order_)
      CancelOrder(order);
    it->second.left_order_.clear();
  }
}

void TakingDemo::process_negative(int64_t ts) {
  constexpr int64_t kStaleNs = 5'000'000'000LL;
  for (auto &id : InstData_.trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    if (!IC || !IC->flag_trading)
      continue;
    auto dd_it = InstData_.depth_map.find(id);
    if (dd_it == InstData_.depth_map.end())
      continue;
    const auto &dd = dd_it->second;
    if (!dd.valid || ts - dd.local_ts > kStaleNs) {
      auto om_it = InstData_.order_map.find(id);
      if (om_it == InstData_.order_map.end())
        continue;
      for (auto *order : om_it->second.left_order_)
        CancelOrder(order);
      om_it->second.left_order_.clear();
      continue;
    }
    auto vol_it = InstData_.vol_map.find(id);
    if (vol_it == InstData_.vol_map.end())
      continue;
    const auto &vd = vol_it->second;
    if (std::isfinite(vd.abs_s_bps) && vd.abs_s_bps > 50.0) {
      auto om_it = InstData_.order_map.find(id);
      if (om_it == InstData_.order_map.end())
        continue;
      for (auto *order : om_it->second.left_order_)
        CancelOrder(order);
      om_it->second.left_order_.clear();
      INFO_FLOG("[Negative] {} abs_s_bps={:.1f} > 50, cancelled", IC->inst_str,
                vd.abs_s_bps);
    }
  }
}

void TakingDemo::process_fp(int64_t ts) {
  DataProcess::fetch_data_all(last_data_info, InstData_, CFG_);
  fpg_->update(ts);
}

// ─── 下单: OB档位漂移表 ───
namespace {
double default_half_spread_bps(data::currency base) {
  switch (base) {
  case data::currency::EUR:
    return 2.0;
  case data::currency::GBP:
    return 3.0;
  case data::currency::BTC:
    return 5.0;
  case data::currency::ETH:
    return 5.0;
  case data::currency::SOL:
    return 10.0;
  case data::currency::XRP:
    return 5.0;
  case data::currency::USDT:
    return 1.0;
  case data::currency::USDC:
    return 1.0;
  default:
    return 5.0;
  }
}
} // namespace

void TakingDemo::process_order(int64_t ts) {
  constexpr int64_t kStaleNs = 5'000'000'000LL;
  constexpr double kOrderUsd = 50.0;
  constexpr int kMaxOrdersPerSide = 3;
  constexpr double kMinPnlBps = 0.5;

  for (auto &id : InstData_.trading_ids) {
    auto *IC = InstData_.IM.FindByUniId(id);
    if (!IC || !IC->flag_trading)
      continue;

    auto dd_it = InstData_.depth_map.find(id);
    if (dd_it == InstData_.depth_map.end())
      continue;
    const auto &dd = dd_it->second;
    if (!dd.valid || ts - dd.local_ts > kStaleNs)
      continue;

    auto vol_it = InstData_.vol_map.find(id);
    if (vol_it != InstData_.vol_map.end()) {
      if (std::isfinite(vol_it->second.abs_s_bps) &&
          vol_it->second.abs_s_bps > 50.0)
        continue;
    }

    double mid = 0.5 * (dd.bids[0][0] + dd.asks[0][0]);
    if (mid <= 0)
      continue;

    double tick = (dd.tick_size > 0) ? dd.tick_size : 1e-5;
    double base_qty = kOrderUsd / mid;
    double qty_prec =
        (IC->quantity_precision > 0) ? IC->quantity_precision : 1e-8;
    base_qty = std::floor(base_qty / qty_prec) * qty_prec;
    if (base_qty <= 0)
      continue;

    // 撤已有订单 (仅当价格变动超过 1 tick 时才撤, 减少订单流失)
    auto om_it = InstData_.order_map.find(id);
    double prev_best_bid = 0.0, prev_best_ask = 0.0;
    if (om_it != InstData_.order_map.end()) {
      for (auto *order : om_it->second.left_order_) {
        if (order->order.side == NOVA_SIDE_BUY) {
          if (order->order.price > prev_best_bid)
            prev_best_bid = order->order.price;
        } else {
          if (prev_best_ask == 0.0 || order->order.price < prev_best_ask)
            prev_best_ask = order->order.price;
        }
      }
    }

    auto *posi = (om_it != InstData_.order_map.end()) ? om_it->second.position_
                                                      : nullptr;
    if (!posi)
      continue;

    struct Candidate {
      int level;
      double pnl;
      double price;
      double fills;
    };
    std::vector<Candidate> ask_cands, bid_cands;

    // 查 drift 表
    auto *dt = [&]() -> DriftTable * {
      auto it = drift_tables_.find(IC->base_str);
      return (it != drift_tables_.end()) ? &it->second : nullptr;
    }();

    for (int lv = 0; lv < 25; ++lv) {
      // ASK
      double ask_p = dd.asks[lv][0];
      if (ask_p > mid && ask_p < mid * 1.1) {
        double pnl = default_half_spread_bps(IC->base);
        double fills = 1.0;
        if (dt && !dt->empty()) {
          auto *entry = dt->lookup(1, lv, 60.0);
          if (entry) {
            pnl = entry->pnl_bps;
            fills = entry->fills_per_day;
          }
        }
        if (pnl > kMinPnlBps)
          ask_cands.push_back({lv, pnl, ask_p, fills});
      }
      // BID
      double bid_p = dd.bids[lv][0];
      if (bid_p > 0 && bid_p < mid) {
        double pnl = default_half_spread_bps(IC->base);
        double fills = 1.0;
        if (dt && !dt->empty()) {
          auto *entry = dt->lookup(0, lv, 60.0);
          if (entry) {
            pnl = entry->pnl_bps;
            fills = entry->fills_per_day;
          }
        }
        if (pnl > kMinPnlBps)
          bid_cands.push_back({lv, pnl, bid_p, fills});
      }
    }

    auto rank = [](const Candidate &a, const Candidate &b) {
      return a.pnl * a.fills > b.pnl * b.fills;
    };
    std::sort(ask_cands.begin(), ask_cands.end(), rank);
    std::sort(bid_cands.begin(), bid_cands.end(), rank);

    int n_ask = std::min(kMaxOrdersPerSide, (int)ask_cands.size());
    int n_bid = std::min(kMaxOrdersPerSide, (int)bid_cands.size());

    // 构建新目标价格集合, 只撤"价格已偏离 >1 tick"的已有订单
    std::set<double> new_ask_px, new_bid_px;
    for (int i = 0; i < n_ask; ++i) {
      double px = std::ceil(ask_cands[i].price / tick) * tick;
      if (px > mid)
        new_ask_px.insert(px);
    }
    for (int i = 0; i < n_bid; ++i) {
      double px = std::floor(bid_cands[i].price / tick) * tick;
      if (px > 0 && px < mid)
        new_bid_px.insert(px);
    }

    if (om_it != InstData_.order_map.end()) {
      // 撤掉不在目标集合中的旧订单
      std::vector<const OrderTp *> to_keep;
      for (auto *order : om_it->second.left_order_) {
        bool keep = false;
        if (order->order.side == NOVA_SIDE_BUY)
          keep = new_bid_px.count(order->order.price) > 0;
        else
          keep = new_ask_px.count(order->order.price) > 0;
        if (keep && order->order_status != NOVA_ORDER_STATUS_CANCELLED &&
            order->order_status != NOVA_ORDER_STATUS_REJECTED)
          to_keep.push_back(order);
        else if (order->order_status != NOVA_ORDER_STATUS_CANCELLED &&
                 order->order_status != NOVA_ORDER_STATUS_FILLED)
          CancelOrder(order);
      }
      om_it->second.left_order_.clear();
      for (auto *o : to_keep)
        om_it->second.left_order_.insert(o);
    }

    // 只发送不在已有订单中的新价格
    for (double px : new_ask_px) {
      bool exists = false;
      if (om_it != InstData_.order_map.end())
        for (auto *o : om_it->second.left_order_)
          if (o->order.side == NOVA_SIDE_SELL && o->order.price == px) {
            exists = true;
            break;
          }
      if (exists)
        continue;
      auto *order = CreateOrder(posi, NOVA_PRICE_LIMIT, NOVA_SIDE_SELL,
                                NOVA_POSITION_EFFECT_OPEN, base_qty, px);
      if (order && SendOrder(order)) {
        if (om_it != InstData_.order_map.end())
          om_it->second.left_order_.insert(order);
      }
    }
    for (double px : new_bid_px) {
      bool exists = false;
      if (om_it != InstData_.order_map.end())
        for (auto *o : om_it->second.left_order_)
          if (o->order.side == NOVA_SIDE_BUY && o->order.price == px) {
            exists = true;
            break;
          }
      if (exists)
        continue;
      auto *order = CreateOrder(posi, NOVA_PRICE_LIMIT, NOVA_SIDE_BUY,
                                NOVA_POSITION_EFFECT_OPEN, base_qty, px);
      if (order && SendOrder(order)) {
        if (om_it != InstData_.order_map.end())
          om_it->second.left_order_.insert(order);
      }
    }
  }
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
    pair_stats_.per_inst.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(IC->uni_id),
                                 std::forward_as_tuple());
    // 标记为做市标的
    IC->flag_trading = true;
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
// 从 left_order_ 中移除已完成的订单, 防止订单指针累积
static void remove_from_orders(data::InstrumentData &InstData,
                               const StrategyApi::OrderTp *order) {
  if (!order)
    return;
  auto *IC = InstData.IM.FindByInstStr(order->order.instrument_id.GetSymbol());
  if (!IC)
    return;
  auto it = InstData.order_map.find(IC->uni_id);
  if (it != InstData.order_map.end())
    it->second.left_order_.erase(order);
}

void TakingDemo::on_order_update(const StrategyApi::OrderTp *order,
                                 const SecurityPosition *) {
  if (order && order->qty_left <= 0)
    remove_from_orders(InstData_, order);
}

void TakingDemo::on_order_cancelled(const StrategyApi::OrderTp *order,
                                    const SecurityPosition *, double) {
  remove_from_orders(InstData_, order);
}

void TakingDemo::on_order_cancel_failed(const StrategyApi::OrderTp *,
                                        const SecurityPosition *, int32_t) {}

void TakingDemo::on_order_rejected(const StrategyApi::OrderTp *order,
                                   const SecurityPosition *, int32_t) {
  remove_from_orders(InstData_, order);
}

void TakingDemo::on_order_done(const StrategyApi::OrderTp *order,
                               const SecurityPosition *, NOVA_ORDER_STATUS) {
  remove_from_orders(InstData_, order);
}

void TakingDemo::on_order_accepted(const StrategyApi::OrderTp *,
                                   const SecurityPosition *) {}

void TakingDemo::on_order_amended(const StrategyApi::OrderTp *,
                                  const SecurityPosition *, int32_t) {}