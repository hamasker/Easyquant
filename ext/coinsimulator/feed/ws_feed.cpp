#include "feed/ws_feed.h"
#include "mock/mock_trade_service.h"
#include "mock/mock_trade_engine.h"
#include "coinrunner_log.h"

#include "nova_api_datainfo.h"
#include "nova_api_instrument.h"
#include "nova_trader_api.h"
#include "base/base_async_log.h"

#include <chrono>
#include <cstdio>
#include <thread>

BEGIN_NOVA_NAMESPACE(trade)

namespace {

int64_t NowNs() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

// 从原始消息中提取交易所原生 symbol
std::string ExtractSymbol(const std::string &exchange,
                          const nlohmann::json &data) {
  if (exchange == "binance" || exchange == "bn" || exchange == "bn_swap") {
    if (data.contains("s")) return data["s"].get<std::string>();
    if (data.contains("stream")) {
      std::string stream = data["stream"].get<std::string>();
      auto pos = stream.find('@');
      return pos != std::string::npos ? stream.substr(0, pos) : stream;
    }
    return "";
  }
  if (exchange == "kraken" || exchange == "krk") {
    // Kraken depth: data is array like [channel, {data}, "pair", ...]
    // Kraken ticker: data is array like [channel, {data}, "pair"]
    if (data.is_array() && data.size() >= 4 && data[3].is_string()) {
      return data[3].get<std::string>();
    }
    if (data.is_array() && data.size() >= 2 && data[2].is_string()) {
      return data[2].get<std::string>();
    }
    return "";
  }
  if (exchange == "okx" || exchange == "ok") {
    if (data.contains("arg") && data["arg"].contains("instId")) {
      return data["arg"]["instId"].get<std::string>();
    }
    if (data.contains("instId")) return data["instId"].get<std::string>();
    return "";
  }
  if (exchange == "coinbase" || exchange == "cb") {
    if (data.contains("product_id")) return data["product_id"].get<std::string>();
    return "";
  }
  if (exchange == "gateio" || exchange == "gt") {
    if (data.contains("result") && data["result"].contains("s")) {
      return data["result"]["s"].get<std::string>();
    }
    return "";
  }
  return "";
}

} // namespace

// ========== WSFeedSpi ==========

class WSFeedSpi final : public nova::ws::WSClientSpi {
public:
  WSFeedSpi(WSFeed &feed, std::string exchange)
      : feed_(feed), exchange_(std::move(exchange)) {}

  void Attach(nova::ws::WSClientApi *api) { api_ = api; }

  void OnFail(nova::ws::WS_ERROR_INFO err) override {
    INFO_FLOG("[WSFeed] {} OnFail: ({}) {}", exchange_, err.code, err.msg);
    feed_.ResetConnection();
  }

  void OnOpen(int group) override {
    INFO_FLOG("[WSFeed] {} connected, group={}", exchange_, group);
    feed_.MarkConnected();
    if (!api_) return;

    if (exchange_ == "binance" || exchange_ == "bn" || exchange_ == "bn_swap") {
      std::vector<std::string> params;
      for (auto &sym : feed_.symbols()) {
        for (auto &ch : feed_.channels()) {
          params.push_back(sym + "@" + ch);
        }
      }
      nlohmann::json sub{{"method", "SUBSCRIBE"}, {"params", params}, {"id", 1}};
      api_->Send(sub.dump());
      INFO_FLOG("[WSFeed] Subscribed to {} binance streams", params.size());
    } else if (exchange_ == "kraken" || exchange_ == "krk") {
      for (auto &sym : feed_.symbols()) {
        for (auto &ch : feed_.channels()) {
          nlohmann::json sub_params{{"name", ch}};
          // book 频道默认只给 10 档, 需要显式指定 depth 才能拿到更多
          if (ch == "book") sub_params["depth"] = 10;  // 10 档初始化更快
          nlohmann::json sub{{"event", "subscribe"},
                             {"pair", {sym}},
                             {"subscription", sub_params}};
          api_->Send(sub.dump());
        }
      }
      INFO_FLOG("[WSFeed] Subscribed to kraken streams");
    } else if (exchange_ == "okx" || exchange_ == "ok") {
      std::vector<nlohmann::json> args;
      for (auto &sym : feed_.symbols()) {
        for (auto &ch : feed_.channels()) {
          args.push_back({{"channel", ch}, {"instId", sym}});
        }
      }
      nlohmann::json sub{{"op", "subscribe"}, {"args", args}};
      api_->Send(sub.dump());
      INFO_FLOG("[WSFeed] Subscribed to {} okx channels", args.size());
    } else if (exchange_ == "coinbase" || exchange_ == "cb") {
      nlohmann::json sub{{"type", "subscribe"},
                         {"product_ids", feed_.symbols()},
                         {"channels", feed_.channels()}};
      api_->Send(sub.dump());
      INFO_FLOG("[WSFeed] Subscribed to coinbase channels");
    }
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    (void)group;
    // OKX/Coinbase 文本心跳: "ping" / "pong"
    if (len <= 4 && msg[0] == 'p') {
      std::string s(msg, len);
      if (s == "ping") { api_->Send("pong", 4); return; }
      if (s == "pong") return;
    }
    // Kraken 纯文本心跳
    if (len > 0 && msg[0] == '{' && strncmp(msg, "{\"event\":\"heartbeat\"", 21) == 0)
      return;
    try {
      auto data = nlohmann::json::parse(std::string_view(msg, len));
      feed_.ProcessRawMessage(exchange_, data);
    } catch (const std::exception &ex) {
      ERROR_FLOG("[WSFeed] {} parse error: {}", exchange_, ex.what());
    }
  }

  void OnClose(bool manual) override {
    INFO_FLOG("[WSFeed] {} OnClose manual={}", exchange_, (int)manual);
    feed_.ResetConnection();
  }

private:
  WSFeed &feed_;
  std::string exchange_;
  nova::ws::WSClientApi *api_ = nullptr;
};

// ========== WSFeed ==========

WSFeed::WSFeed() {}

WSFeed::~WSFeed() { Stop(); }

// 通用频道名 → 交易所原生频道 (自动选最快数据源)
static std::vector<std::string>
MapChannels(const std::string &exchange,
            const std::vector<std::string> &generic) {
  std::vector<std::string> out;
  for (auto &ch : generic) {
    if (exchange == "binance" || exchange == "bn") {
      if (ch == "bbo") out.push_back("bookTicker");
      else if (ch == "depth") out.push_back("depth@100ms");
      else if (ch == "trade") out.push_back("trade");
      else out.push_back(ch);
    } else if (exchange == "bn_swap") {
      if (ch == "bbo") out.push_back("bookTicker");
      else if (ch == "depth") out.push_back("depth@100ms");
      else if (ch == "trade") out.push_back("trade");
      else out.push_back(ch);
    } else if (exchange == "kraken" || exchange == "krk") {
      if (ch == "bbo") out.push_back("ticker");
      else if (ch == "depth") out.push_back("book");  // depth=25 在 OnOpen 里设
      else if (ch == "trade") out.push_back("trade");
      else out.push_back(ch);
    } else if (exchange == "okx" || exchange == "ok") {
      if (ch == "bbo") out.push_back("bbo-tbt");
      else if (ch == "depth") out.push_back("books5");
      else if (ch == "trade") {} // OKX 公开频道无 trade
      else out.push_back(ch);
    } else if (exchange == "coinbase" || exchange == "cb") {
      if (ch == "bbo") out.push_back("ticker");
      else if (ch == "depth") out.push_back("level2");
      else if (ch == "trade") out.push_back("matches");
      else out.push_back(ch);
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

bool WSFeed::Initialize(const nova::base::Config &cfg) {
  (void)cfg;
  if (exchange_.empty()) exchange_ = "binance";
  if (symbols_.empty()) symbols_ = {"btcusdt"};
  if (channels_.empty()) channels_ = {"bbo"};

  // 通用频道名 → 交易所原生频道
  channels_ = MapChannels(exchange_, channels_);

  // config ws_front_address 优先
  if (!ws_url_override_.empty()) {
    ws_url_ = ws_url_override_;
    INFO_FLOG("[WSFeed] Init: {} → {}", exchange_, ws_url_);
    return true;
  }

  if (exchange_ == "binance" || exchange_ == "bn") {
    ws_url_ = "wss://stream.binance.com:9443/ws";
  } else if (exchange_ == "bn_swap") {
    ws_url_ = "wss://fstream.binance.com/ws";
  } else if (exchange_ == "kraken" || exchange_ == "krk") {
    ws_url_ = "wss://ws.kraken.com";
  } else if (exchange_ == "okx" || exchange_ == "ok") {
    ws_url_ = "wss://ws.okx.com:8443/ws/v5/public";
  } else if (exchange_ == "coinbase" || exchange_ == "cb") {
    ws_url_ = "wss://ws-feed.exchange.coinbase.com";
  } else {
    ERROR_FLOG("[WSFeed] Unknown exchange: {}", exchange_);
    return false;
  }

  INFO_FLOG("[WSFeed] Init: {} {} symbols, {} channels", exchange_,
            symbols_.size(), channels_.size());
  return true;
}

bool WSFeed::Poll() {
  if (!running_) return false;

  if (!connected_) {
    ConnectAndSubscribe();
    connected_ = true;
    connect_ts_ = NowNs();
  }

  if (ws_client_) {
    auto st = ws_client_->state();
    if (st == nova::ws::WS_STATE_OPEN) {
      connect_ts_ = 0; // 连接成功, 清除时间戳
      // 心跳: 20s 无数据则发 ping 保活
      if (last_data_ns_ > 0 && NowNs() - last_data_ns_ > 20'000'000'000LL) {
        INFO_FLOG("[WSFeed] {} heartbeat ping", exchange_);
        ws_client_->Send("ping", 4);
        last_data_ns_ = NowNs();
      }
    } else if (st == nova::ws::WS_STATE_CLOSED && connect_ts_ > 0) {
      // 连接失败: 等待至少 2s 后重试
      if (NowNs() - connect_ts_ > 2'000'000'000LL) {
        INFO_FLOG("[WSFeed] {} retry after fail", exchange_);
        ConnectAndSubscribe();
        connect_ts_ = NowNs();
      }
    }
    // CONNECTING 状态: 等待 (connect_ts_ 不为 0)
    // 如果卡在 CONNECTING 超过 10s, 强制重连
    if (st == nova::ws::WS_STATE_CONNECTING && connect_ts_ > 0) {
      if (NowNs() - connect_ts_ > 10'000'000'000LL) {
        INFO_FLOG("[WSFeed] {} timeout, retry", exchange_);
        ConnectAndSubscribe();
        connect_ts_ = NowNs();
      }
    }
  }

  // 仅空闲时短暂休眠, 有数据/连接时不等待
  if (ws_client_ && ws_client_->state() == nova::ws::WS_STATE_OPEN &&
      last_data_ns_ > 0 && (NowNs() - last_data_ns_) < 1'000'000'000LL) {
    std::this_thread::yield();
  }
  return running_;
}

void WSFeed::Stop() {
  INFO_FLOG("[WSFeed] {} Stop called", exchange_);
  running_ = false;
  if (ws_client_) {
    ws_client_->Stop();
  }
}

void WSFeed::ConnectAndSubscribe() {
  INFO_FLOG("[WSFeed] {} connecting to {}", exchange_, ws_url_);
  auto *spi = new WSFeedSpi(*this, exchange_);
  ws_client_ = nova::ws::WSClientApi::Create(spi, true, 0);
  spi->Attach(ws_client_);

  auto err = ws_client_->Initialize(ws_url_.c_str(), core_, 10, false);
  if (err.code != nova::ws::WS_OK) {
    ERROR_FLOG("[WSFeed] {} WS init failed: ({}) {}", exchange_, err.code,
               err.msg);
    return;
  }

  INFO_FLOG("[WSFeed] {} connected OK", exchange_);
}

// ========== 消息处理 ==========

void WSFeed::ProcessRawMessage(const std::string &exchange,
                               const nlohmann::json &data) {
  if (data.contains("result") && data.contains("id") && !data.contains("e"))
    return;
  // 跳过 Kraken 订阅确认/状态/心跳消息 (非行情, 无 instrument_id)
  if ((exchange == "krk" || exchange == "kraken") && data.is_object()) {
    if (data.contains("event") || data.contains("errorMessage")) return;
  }

  last_data_ns_ = NowNs(); // 更新心跳时间

  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;

  // Kraken 本地订单簿 (增量 → 全档合成)
  static std::unordered_map<std::string,
      std::pair<std::map<double, double, std::greater<double>>,
                std::map<double, double>>> local_book;

  int64_t local_ns = NowNs();

  // 提取交易所原生 symbol -> InstrumentId
  auto raw_sym = ExtractSymbol(exchange, data);
  InstrumentId inst_id{};
  if (!raw_sym.empty() && !symbol_to_inst_.empty()) {
    // 先精确查找
    auto it = symbol_to_inst_.find(raw_sym);
    if (it != symbol_to_inst_.end()) {
      inst_id = it->second;
    } else {
      // 大小写不敏感查找
      std::string raw_lower = raw_sym;
      std::transform(raw_lower.begin(), raw_lower.end(), raw_lower.begin(), ::tolower);
      for (auto &[key, val] : symbol_to_inst_) {
        std::string key_lower = key;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        if (raw_lower == key_lower) {
          inst_id = val;
          break;
        }
      }
    }
  }

  static std::unordered_map<std::string, int> dispatch_cnt;
  auto dispatch = [&](NOVA_COIN_QUOTE_TYPE qtype, const void *ptr, size_t size) {
    // 优先精确匹配 instrument_id
    for (size_t i = 0; i < state.subs.size(); ++i) {
      if (state.subs[i].quote_type == qtype &&
          state.subs[i].position &&
          state.subs[i].position->instrument.key() == inst_id.key()) {
        di_mgr->Push(i, ptr, local_ns);
        if (state.subs[i].trigger && state.strategy) {
          if (++dispatch_cnt[exchange] <= 3)
            INFO_FLOG("[WSFeed] dispatch {} qtype={} di={} inst={}", exchange_,
                    (int)qtype, i, inst_id.symbol);
          state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                       state.subs[i].position);
        }
        // BBO 同步到撮合引擎
        if (qtype == NOVA_COIN_QUOTE_BBO && inst_id.Valid()) {
          auto *bbo = static_cast<const NovaCoinBBO *>(ptr);
          for (auto *engine : state.engines) {
            auto *mock = dynamic_cast<MockTradeEngine *>(engine);
            if (mock && engine->Exchange() == inst_id.exchange) {
              mock->UpdateBBO(inst_id, bbo->bid_price, bbo->ask_price,
                              bbo->bid_qty, bbo->ask_qty);
            }
          }
        }
        return;
      }
    }
    // 退化: 匹配第一个同类型的订阅
    for (size_t i = 0; i < state.subs.size(); ++i) {
      if (state.subs[i].quote_type == qtype) {
        di_mgr->Push(i, ptr, local_ns);
        if (state.subs[i].trigger && state.strategy) {
          if (++dispatch_cnt[exchange] <= 3)
            WARNING_FLOG("[WSFeed] dispatch {} (fallback) qtype={} di={}", exchange_,
                    (int)qtype, i);
          state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                       state.subs[i].position);
        }
        return;
      }
    }
    (void)size;
  };

  // Binance 单 symbol 格式: {"e":"trade"/"depthUpdate", "s":"BTCUSDT", ...}
  // Binance 组合流: {"stream":"btcusdt@bookTicker", "data":{...}}
  auto *payload = &data;
  if (data.contains("stream") && data.contains("data")) {
    payload = &data["data"];
    // 重新提取 symbol (组合流中可能在 stream 字段里)
    if (raw_sym.empty()) {
      std::string stream = data["stream"].get<std::string>();
      auto pos = stream.find('@');
      if (pos != std::string::npos) raw_sym = stream.substr(0, pos);
    }
  }

  // Binance depthUpdate: b/a 是数组, e == "depthUpdate"
  // 必须在 BBO 检查之前, 因为 depth 也有 b/a 键
  if (payload->contains("b") && payload->contains("a") &&
      (*payload)["b"].is_array() && (*payload)["a"].is_array()) {
    NovaCoinDepth depth{};
    if (inst_id.Valid()) depth.instrument_id = inst_id;
    depth.update_time = payload->value("E", int64_t{0}) * 1'000'000;
    depth.local_ns = local_ns;

    const auto &bids = (*payload)["b"];
    const auto &asks = (*payload)["a"];
    size_t bc = std::min(bids.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
    size_t ac = std::min(asks.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
    for (size_t i = 0; i < bc; ++i) {
      depth.bid[i].price = bids[i][0].is_string()
                               ? std::stod(bids[i][0].get<std::string>())
                               : bids[i][0].get<double>();
      depth.bid[i].qty = bids[i][1].is_string()
                              ? std::stod(bids[i][1].get<std::string>())
                              : bids[i][1].get<double>();
    }
    for (size_t i = 0; i < ac; ++i) {
      depth.ask[i].price = asks[i][0].is_string()
                               ? std::stod(asks[i][0].get<std::string>())
                               : asks[i][0].get<double>();
      depth.ask[i].qty = asks[i][1].is_string()
                              ? std::stod(asks[i][1].get<std::string>())
                              : asks[i][1].get<double>();
    }
    depth.ob_level = static_cast<uint16_t>(std::max(bc, ac));
    dispatch(NOVA_COIN_QUOTE_DEPTH, &depth, sizeof(depth));
    return;
  }

  // BBO / bookTicker (single binance stream or standalone)
  if (payload->contains("b") && payload->contains("a") &&
      !(*payload)["b"].is_array()) {
    NovaCoinBBO bbo{};
    if (inst_id.Valid()) bbo.instrument_id = inst_id;
    bbo.bid_price = (*payload)["b"].is_string()
                        ? std::stod((*payload)["b"].get<std::string>())
                        : (*payload)["b"].get<double>();
    bbo.bid_qty = (*payload)["B"].is_string()
                      ? std::stod((*payload)["B"].get<std::string>())
                      : (*payload)["B"].get<double>();
    bbo.ask_price = (*payload)["a"].is_string()
                        ? std::stod((*payload)["a"].get<std::string>())
                        : (*payload)["a"].get<double>();
    bbo.ask_qty = (*payload)["A"].is_string()
                      ? std::stod((*payload)["A"].get<std::string>())
                      : (*payload)["A"].get<double>();
    bbo.update_time = payload->value("E", int64_t{0}) * 1'000'000;
    bbo.local_ns = local_ns;
    dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
    return;
  }

  // Depth (general)
  if (payload->contains("bids") && payload->contains("asks")) {
    NovaCoinDepth depth{};
    if (inst_id.Valid()) depth.instrument_id = inst_id;
    depth.update_time = payload->value("E", int64_t{0}) * 1'000'000;
    depth.local_ns = local_ns;

    const auto &bids = (*payload)["bids"];
    const auto &asks = (*payload)["asks"];
    size_t bc = std::min(bids.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
    size_t ac = std::min(asks.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
    for (size_t i = 0; i < bc; ++i) {
      depth.bid[i].price = bids[i][0].is_string()
                               ? std::stod(bids[i][0].get<std::string>())
                               : bids[i][0].get<double>();
      depth.bid[i].qty = bids[i][1].is_string()
                              ? std::stod(bids[i][1].get<std::string>())
                              : bids[i][1].get<double>();
    }
    for (size_t i = 0; i < ac; ++i) {
      depth.ask[i].price = asks[i][0].is_string()
                               ? std::stod(asks[i][0].get<std::string>())
                               : asks[i][0].get<double>();
      depth.ask[i].qty = asks[i][1].is_string()
                              ? std::stod(asks[i][1].get<std::string>())
                              : asks[i][1].get<double>();
    }
    depth.ob_level = static_cast<uint16_t>(std::max(bc, ac));
    dispatch(NOVA_COIN_QUOTE_DEPTH, &depth, sizeof(depth));
    return;
  }

  // Trade
  if (payload->contains("e") && (*payload)["e"] == "trade") {
    NovaCoinTrade trade{};
    if (inst_id.Valid()) trade.instrument_id = inst_id;
    trade.price = (*payload)["p"].is_string()
                      ? std::stod((*payload)["p"].get<std::string>())
                      : (*payload)["p"].get<double>();
    trade.qty = (*payload)["q"].is_string()
                    ? std::stod((*payload)["q"].get<std::string>())
                    : (*payload)["q"].get<double>();
    trade.timestamp = payload->value("T", int64_t{0}) * 1'000'000;
    trade.local_ns = local_ns;
    trade.side = payload->value("m", false) ? NOVA_SIDE_SELL : NOVA_SIDE_BUY;
    dispatch(NOVA_COIN_QUOTE_TRADE, &trade, sizeof(trade));
    return;
  }

  // Kraken 数组格式: [channelID, data, "pair", "pair"]
  if (data.is_array() && data.size() >= 3) {
    if (!data[1].is_object()) return;

    const auto &d = data[1];
    // Kraken ticker: b/a 是一维数组 [price, volume, ts]
    // (book 更新时 b/a 是二维数组 [[price,qty,ts],...], 用 !b[0].is_array() 区分)
    if (d.contains("b") && d.contains("a") && d["b"].is_array() &&
        d["a"].is_array() && !d["b"].empty() && !d["b"][0].is_array()) {
      NovaCoinBBO bbo{};
      if (inst_id.Valid()) bbo.instrument_id = inst_id;
      bbo.bid_price = d["b"][0].is_string()
                          ? std::stod(d["b"][0].get<std::string>())
                          : d["b"][0].get<double>();
      bbo.bid_qty = d["b"][1].is_string()
                        ? std::stod(d["b"][1].get<std::string>())
                        : d["b"][1].get<double>();
      bbo.ask_price = d["a"][0].is_string()
                          ? std::stod(d["a"][0].get<std::string>())
                          : d["a"][0].get<double>();
      bbo.ask_qty = d["a"][1].is_string()
                        ? std::stod(d["a"][1].get<std::string>())
                        : d["a"][1].get<double>();
      bbo.update_time = local_ns;
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      return;
    }
    // Kraken book 增量更新: 维护本地订单簿, 合成全档 snapshot
    if ((d.contains("b") && d["b"].is_array() && !d["b"].empty() &&
         d["b"][0].is_array()) ||
        (d.contains("a") && d["a"].is_array() && !d["a"].empty() &&
         d["a"][0].is_array())) {
      if (!inst_id.Valid()) return;
      auto &[bids, asks] = local_book[inst_id.key()];
      auto apply = [&](const nlohmann::json &arr, bool is_bid) {
        for (auto &entry : arr) {
          if (!entry.is_array() || entry.size() < 2) continue;
          double px = entry[0].is_string() ? std::stod(entry[0].get<std::string>())
                                           : entry[0].get<double>();
          double qty = entry[1].is_string() ? std::stod(entry[1].get<std::string>())
                                            : entry[1].get<double>();
          if (qty <= 0 || std::isnan(px)) {
            if (is_bid) bids.erase(px); else asks.erase(px);
          } else {
            if (is_bid) bids[px] = qty; else asks[px] = qty;
          }
        }
      };
      if (d.contains("b") && d["b"].is_array()) apply(d["b"], true);
      if (d.contains("a") && d["a"].is_array()) apply(d["a"], false);

      // 合成全档: DEPTH_LVN (100档) 或 DEPTH (25档)
      NovaCoinDepthLVN depth{};
      depth.instrument_id = inst_id;
      depth.update_time = local_ns;
      depth.local_time = local_ns;
      depth.local_ns = local_ns;
      int i = 0;
      for (auto &[px, qty] : bids) {
        if (i >= NovaCoinDepthLVN::MAX_PRICE_LEVEL) break;
        depth.bid[i].price = px;
        depth.bid[i].qty = qty;
        ++i;
      }
      depth.ob_level = static_cast<uint16_t>(i);
      i = 0;
      for (auto &[px, qty] : asks) {
        if (i >= NovaCoinDepthLVN::MAX_PRICE_LEVEL) break;
        depth.ask[i].price = px;
        depth.ask[i].qty = qty;
        ++i;
      }
      depth.ob_level = static_cast<uint16_t>(
          std::max((int)depth.ob_level, i));
      dispatch(NOVA_COIN_QUOTE_DEPTH_LVN, &depth, sizeof(depth));
      return;
    }
    // Kraken depth (book snapshot: bs/as → 初始化本地订单簿)
    if ((d.contains("bs") || d.contains("as"))) {
      if (!inst_id.Valid()) return;
      auto &[bids, asks] = local_book[inst_id.key()];
      bids.clear(); asks.clear();
      auto fill = [](const nlohmann::json &arr, auto &map) {
        for (auto &entry : arr) {
          if (!entry.is_array() || entry.size() < 2) continue;
          double px = entry[0].is_string() ? std::stod(entry[0].get<std::string>())
                                           : entry[0].get<double>();
          double qty = entry[1].is_string() ? std::stod(entry[1].get<std::string>())
                                            : entry[1].get<double>();
          if (qty > 0) map[px] = qty;
        }
      };
      if (d.contains("bs")) fill(d["bs"], bids);
      if (d.contains("as")) fill(d["as"], asks);

      NovaCoinDepthLVN depth{};
      depth.instrument_id = inst_id;
      depth.update_time = local_ns;
      depth.local_time = local_ns;
      depth.local_ns = local_ns;
      int i = 0;
      for (auto &[px, qty] : bids) {
        if (i >= NovaCoinDepthLVN::MAX_PRICE_LEVEL) break;
        depth.bid[i].price = px; depth.bid[i].qty = qty; ++i;
      }
      depth.ob_level = static_cast<uint16_t>(i);
      i = 0;
      for (auto &[px, qty] : asks) {
        if (i >= NovaCoinDepthLVN::MAX_PRICE_LEVEL) break;
        depth.ask[i].price = px; depth.ask[i].qty = qty; ++i;
      }
      depth.ob_level = static_cast<uint16_t>(std::max((int)depth.ob_level, i));
      dispatch(NOVA_COIN_QUOTE_DEPTH_LVN, &depth, sizeof(depth));
      return;
    }
  }

  // OKX 格式
  if (data.contains("arg") && data.contains("data") && data["data"].is_array()) {
    const auto &arg = data["arg"];
    std::string ch = arg.value("channel", "");
    const auto &items = data["data"];
    if (items.empty()) return;

    if (ch == "tickers") {
      const auto &d = items[0];
      NovaCoinBBO bbo{};
      if (inst_id.Valid()) bbo.instrument_id = inst_id;
      auto get_num = [&](const char *key) -> double {
        if (!d.contains(key)) return 0;
        auto &v = d[key];
        return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
      };
      bbo.bid_price = get_num("bidPx");
      bbo.bid_qty = get_num("bidSz");
      bbo.ask_price = get_num("askPx");
      bbo.ask_qty = get_num("askSz");
      bbo.update_time = static_cast<int64_t>(get_num("ts")) * 1'000'000;
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      return;
    }
    if (ch == "bbo-tbt") {
      // bbo-tbt 格式: data[0].bids[0]=[price,qty,0,orders], data[0].asks[0]=[price,qty,0,orders]
      const auto &d = items[0];
      NovaCoinBBO bbo{};
      if (inst_id.Valid()) bbo.instrument_id = inst_id;
      auto get_bb = [&](const char *side, int idx) -> double {
        if (!d.contains(side) || !d[side].is_array() || d[side].empty()) return 0;
        auto &arr = d[side][0]; // 取最优档
        if (!arr.is_array() || arr.size() <= (size_t)idx) return 0;
        auto &v = arr[idx];
        return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
      };
      bbo.bid_price = get_bb("bids", 0);
      bbo.bid_qty = get_bb("bids", 1);
      bbo.ask_price = get_bb("asks", 0);
      bbo.ask_qty = get_bb("asks", 1);
      if (d.contains("ts")) {
        auto &tsv = d["ts"];
        bbo.update_time = tsv.is_string()
            ? static_cast<int64_t>(std::stod(tsv.get<std::string>()) * 1'000'000)
            : static_cast<int64_t>(tsv.get<double>() * 1'000'000);
      }
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      return;
    }
    if (ch == "books" || ch == "books5") {
      const auto &d = items[0];
      NovaCoinDepth depth{};
      if (inst_id.Valid()) depth.instrument_id = inst_id;
      auto get_num2 = [&](const char *key) -> double {
        if (!d.contains(key)) return 0;
        auto &v = d[key];
        return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
      };
      depth.update_time = static_cast<int64_t>(get_num2("ts")) * 1'000'000;
      depth.local_ns = local_ns;
      if (d.contains("bids")) {
        const auto &bids = d["bids"];
        size_t n = std::min(bids.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
        for (size_t i = 0; i < n; ++i) {
          if (bids[i].size() > 0) {
            auto &v0 = bids[i][0];
            depth.bid[i].price = v0.is_string() ? std::stod(v0.get<std::string>()) : v0.get<double>();
          }
          if (bids[i].size() > 1) {
            auto &v1 = bids[i][1];
            depth.bid[i].qty = v1.is_string() ? std::stod(v1.get<std::string>()) : v1.get<double>();
          }
        }
        depth.ob_level = static_cast<uint16_t>(n);
      }
      if (d.contains("asks")) {
        const auto &asks = d["asks"];
        size_t n = std::min(asks.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
        for (size_t i = 0; i < n; ++i) {
          if (asks[i].size() > 0) {
            auto &v0 = asks[i][0];
            depth.ask[i].price = v0.is_string() ? std::stod(v0.get<std::string>()) : v0.get<double>();
          }
          if (asks[i].size() > 1) {
            auto &v1 = asks[i][1];
            depth.ask[i].qty = v1.is_string() ? std::stod(v1.get<std::string>()) : v1.get<double>();
          }
        }
        depth.ob_level = static_cast<uint16_t>(std::max((int)depth.ob_level, (int)n));
      }
      dispatch(NOVA_COIN_QUOTE_DEPTH, &depth, sizeof(depth));
    }
    return;
  }

  // Coinbase 格式
  if (data.contains("type") && data["type"] == "ticker") {
    NovaCoinBBO bbo{};
    if (inst_id.Valid()) bbo.instrument_id = inst_id;
    bbo.bid_price = data["best_bid"].is_string()
                        ? std::stod(data["best_bid"].get<std::string>())
                        : data["best_bid"].get<double>();
    bbo.bid_qty = data["best_bid_size"].is_string()
                      ? std::stod(data["best_bid_size"].get<std::string>())
                      : data["best_bid_size"].get<double>();
    bbo.ask_price = data["best_ask"].is_string()
                        ? std::stod(data["best_ask"].get<std::string>())
                        : data["best_ask"].get<double>();
    bbo.ask_qty = data["best_ask_size"].is_string()
                      ? std::stod(data["best_ask_size"].get<std::string>())
                      : data["best_ask_size"].get<double>();
    bbo.update_time = local_ns;
    bbo.local_ns = local_ns;
    dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
    return;
  }
  if (data.contains("type") && data["type"] == "snapshot") {
    NovaCoinDepth depth{};
    if (inst_id.Valid()) depth.instrument_id = inst_id;
    depth.update_time = local_ns;
    depth.local_ns = local_ns;
    if (data.contains("bids")) {
      const auto &bids = data["bids"];
      size_t n = std::min(bids.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
      for (size_t i = 0; i < n; ++i) {
        depth.bid[i].price = bids[i][0].is_string()
                                 ? std::stod(bids[i][0].get<std::string>())
                                 : bids[i][0].get<double>();
        depth.bid[i].qty = bids[i][1].is_string()
                                ? std::stod(bids[i][1].get<std::string>())
                                : bids[i][1].get<double>();
      }
      depth.ob_level = static_cast<uint16_t>(n);
    }
    if (data.contains("asks")) {
      const auto &asks = data["asks"];
      size_t n = std::min(asks.size(), (size_t)NovaCoinDepth::MAX_PRICE_LEVEL);
      for (size_t i = 0; i < n; ++i) {
        depth.ask[i].price = asks[i][0].is_string()
                                 ? std::stod(asks[i][0].get<std::string>())
                                 : asks[i][0].get<double>();
        depth.ask[i].qty = asks[i][1].is_string()
                                ? std::stod(asks[i][1].get<std::string>())
                                : asks[i][1].get<double>();
      }
      depth.ob_level = static_cast<uint16_t>(std::max((int)depth.ob_level, (int)n));
    }
    dispatch(NOVA_COIN_QUOTE_DEPTH, &depth, sizeof(depth));
    return;
  }
}

END_NOVA_NAMESPACE(trade)
