#include "feed/ws_feed.h"
#include "mock/mock_trade_service.h"
#include "mock/mock_trade_engine.h"
#include "coinrunner_log.h"

#include "nova_api_datainfo.h"
#include "nova_api_instrument.h"
#include "nova_trader_api.h"
#include "base/base_async_log.h"

#pragma push_macro("Prefetch")
#undef Prefetch
#include "PushDataV3ApiWrapper.pb.h"
#pragma pop_macro("Prefetch")

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
    // v2: {"channel":"...","data":[{"symbol":"BTC/USD"},...]}
    if (data.contains("data") && data["data"].is_array() &&
        !data["data"].empty() && data["data"][0].is_object())
      return data["data"][0].value("symbol", "");
    // v1 fallback
    if (data.is_array() && data.size() >= 4 && data[3].is_string())
      return data[3].get<std::string>();
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
    // Advanced Trade: 从 events[0].product_id 提取
    if (data.contains("events") && data["events"].is_array() &&
        !data["events"].empty() && data["events"][0].contains("product_id")) {
      return data["events"][0]["product_id"].get<std::string>();
    }
    return "";
  }
  if (exchange == "gateio" || exchange == "gt") {
    // 新 JSON-RPC: params[0] 或 params[2] 是 symbol
    if (data.contains("params") && data["params"].is_array()) {
      const auto &p = data["params"];
      std::string method = data.value("method", "");
      if (method == "depth.update" && p.size() >= 3 && p[2].is_string()) {
        return p[2].get<std::string>();
      }
      if (p.size() >= 1 && p[0].is_string()) {
        return p[0].get<std::string>();
      }
    }
    return "";
  }
  if (exchange == "mexc") {
    // MEXC channel 格式: "spot@public.bookTicker.v3.api@BTCUSDT"
    if (data.contains("c")) {
      std::string ch = data["c"].get<std::string>();
      auto pos = ch.rfind('@');
      return pos != std::string::npos ? ch.substr(pos + 1) : "";
    }
    if (data.contains("symbol")) return data["symbol"].get<std::string>();
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
      // v2: JSON-RPC 订阅, 每个频道一组 symbol; XBT→BTC 转换
      for (auto &ch : feed_.channels()) {
        nlohmann::json syms = nlohmann::json::array();
        for (auto &s : feed_.symbols()) {
          std::string sym = s;
          // v2 用 BTC 而非 XBT
          size_t p = sym.find("XBT");
          if (p != std::string::npos) sym.replace(p, 3, "BTC");
          syms.push_back(sym);
        }
        nlohmann::json params{{"channel", ch}, {"symbol", syms}};
        if (ch == "book") params["depth"] = 10;
        if (ch == "trade") params["snapshot"] = false;
        nlohmann::json sub{{"method", "subscribe"}, {"params", params}};
        api_->Send(sub.dump());
      }
      INFO_FLOG("[WSFeed] Subscribed to {} kraken v2 channels", feed_.channels().size());
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
      // Exchange WS: 一次订阅所有频道
      nlohmann::json sub{{"type", "subscribe"},
                         {"product_ids", feed_.symbols()},
                         {"channels", feed_.channels()}};
      api_->Send(sub.dump());
      INFO_FLOG("[WSFeed] Subscribed to {} coinbase channels", feed_.channels().size());
    } else if (exchange_ == "gateio" || exchange_ == "gt") {
      static int gate_id = 0;
      for (auto &ch : feed_.channels()) {
        std::string method = ch + ".subscribe";
        if (ch == "depth") {
          // depth 需要 3 个参数: [symbol, limit, "0"]
          for (auto &sym : feed_.symbols()) {
            nlohmann::json sub{{"method", method},
                               {"params", nlohmann::json::array({sym, 1, "0"})},
                               {"id", ++gate_id},
                               {"time", (int64_t)(NowNs() / 1'000'000'000)}};
            api_->Send(sub.dump());
          }
        } else {
          nlohmann::json sub{{"method", method},
                             {"params", feed_.symbols()},
                             {"id", ++gate_id},
                             {"time", (int64_t)(NowNs() / 1'000'000'000)}};
          api_->Send(sub.dump());
        }
      }
      INFO_FLOG("[WSFeed] Subscribed to {} gateio channels", feed_.channels().size());
    } else if (exchange_ == "mexc") {
      // MEXC: 每个 symbol+channel 组合一个 param
      std::vector<std::string> params;
      for (auto &sym : feed_.symbols()) {
        for (auto &ch : feed_.channels()) {
          params.push_back(ch + "@" + sym);
        }
      }
      nlohmann::json sub{{"method", "SUBSCRIPTION"}, {"params", params}};
      api_->Send(sub.dump());
      INFO_FLOG("[WSFeed] Subscribed to {} mexc streams", params.size());
    }
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    (void)group;
    // debug: 打印 gateio 前几条原始消息
    if ((exchange_ == "gt" || exchange_ == "mexc") && len > 0) {
      static int _raw_cnt = 0;
      if (++_raw_cnt <= 5) {
        bool is_bin = (exchange_ == "mexc" && len > 0 && msg[0] != '{');
        if (is_bin) {
          WARNING_FLOG("[WSFeed] {} raw[{}]: binary {} bytes", exchange_, _raw_cnt, len);
        } else {
          std::string _raw(msg, std::min(len, (size_t)400));
          WARNING_FLOG("[WSFeed] {} raw[{}]: {}", exchange_, _raw_cnt, _raw);
        }
      }
    }
    // OKX/Coinbase 文本心跳: "ping" / "pong"
    if (len <= 4 && msg[0] == 'p') {
      std::string s(msg, len);
      if (s == "ping") { api_->Send("pong", 4); return; }
      if (s == "pong") return;
    }
    // Kraken 纯文本心跳
    if (len > 0 && msg[0] == '{' && strncmp(msg, "{\"event\":\"heartbeat\"", 21) == 0)
      return;
    // Mexc 二进制 Protobuf 消息
    if (exchange_ == "mexc" && len > 0 && msg[0] != '{') {
      feed_.ProcessMexcProto(msg, len);
      return;
    }
    // Mexc JSON ping: {"method":"PING"}
    if (exchange_ == "mexc" && len > 10 && strncmp(msg, "{\"method\":\"PING\"}", 16) == 0) {
      static const char pong[] = "{\"method\":\"PONG\"}";
      api_->Send(pong, sizeof(pong) - 1);
      return;
    }
    try {
      auto data = nlohmann::json::parse(std::string_view(msg, len));
      feed_.ProcessRawMessage(exchange_, data);
    } catch (const std::exception &ex) {
      // Kraken v2 可能多条 JSON 拼在一个 frame, 换行分割逐条 parse
      if (exchange_ == "krk" || exchange_ == "kraken") {
        std::string raw(msg, len);
        size_t start = 0;
        bool ok = false;
        for (size_t i = 0; i <= raw.size(); ++i) {
          if (i == raw.size() || raw[i] == '\n') {
            if (i > start) {
              try {
                auto j = nlohmann::json::parse(raw.substr(start, i - start));
                feed_.ProcessRawMessage(exchange_, j);
                ok = true;
              } catch (...) {}
            }
            start = i + 1;
          }
        }
        if (ok) return;
      }
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
      if (ch == "bbo") out.push_back("ticker");        // v2
      else if (ch == "depth") out.push_back("book");   // v2
      else if (ch == "trade") out.push_back("trade");  // v2
      else out.push_back(ch);
    } else if (exchange == "okx" || exchange == "ok") {
      if (ch == "bbo") out.push_back("bbo-tbt");
      else if (ch == "depth") out.push_back("books5");
      else if (ch == "trade") out.push_back("trades");
      else out.push_back(ch);
    } else if (exchange == "coinbase" || exchange == "cb") {
      if (ch == "bbo") out.push_back("ticker");       // Exchange WS: ticker 推送 BBO
      else if (ch == "depth") out.push_back("level2");
      else if (ch == "trade") out.push_back("matches"); // Exchange WS: matches 推送逐笔成交
      else out.push_back(ch);
    } else if (exchange == "gateio" || exchange == "gt") {
      // 新 JSON-RPC: bbo → depth(limit=1), trade → trades
      if (ch == "bbo") out.push_back("depth");
      else if (ch == "trade") out.push_back("trades");
      else if (ch == "depth") out.push_back("depth");
      else out.push_back(ch);
    } else if (exchange == "mexc") {
      if (ch == "bbo") out.push_back("spot@public.aggre.bookTicker.v3.api.pb@10ms");
      else if (ch == "trade") out.push_back("spot@public.aggre.deals.v3.api.pb@10ms");
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
    ws_url_ = "wss://ws.kraken.com/v2";
  } else if (exchange_ == "okx" || exchange_ == "ok") {
    ws_url_ = "wss://ws.okx.com:8443/ws/v5/public";
  } else if (exchange_ == "coinbase" || exchange_ == "cb") {
    ws_url_ = "wss://ws-feed.exchange.coinbase.com";  // Exchange WS (公开, 无需认证)
  } else if (exchange_ == "gateio" || exchange_ == "gt") {
    ws_url_ = "wss://ws.gateio.ws/v4/ws";
  } else if (exchange_ == "mexc") {
    ws_url_ = "wss://wbs-api.mexc.com/ws";
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
      connect_ts_ = 0;       // 连接成功, 清除时间戳
      retry_count_ = 0;      // 重置重连计数
      // 心跳: 20s 无数据则发 ping 保活
      if (last_data_ns_ > 0 && NowNs() - last_data_ns_ > 20'000'000'000LL) {
        INFO_FLOG("[WSFeed] {} heartbeat ping", exchange_);
        ws_client_->Send("ping", 4);
        last_data_ns_ = NowNs();
      }
      // 数据超时检测: 60s 无任何数据, 强制断开重连
      if (last_data_ns_ > 0 && NowNs() - last_data_ns_ > 60'000'000'000LL) {
        INFO_FLOG("[WSFeed] {} data timeout (60s), force reconnect", exchange_);
        ws_client_->Stop();
        ResetConnection();
      }
    } else if (st == nova::ws::WS_STATE_CLOSED && connect_ts_ > 0) {
      // 指数退避重试: 2s → 4s → 8s → ... → 60s
      int64_t backoff_s = 2LL << retry_count_;
      if (backoff_s > 60) backoff_s = 60;
      int64_t backoff_ns = backoff_s * 1'000'000'000LL;
      if (NowNs() - connect_ts_ > backoff_ns) {
        ++retry_count_;
        INFO_FLOG("[WSFeed] {} retry #{} after {}s backoff", exchange_,
                  retry_count_, backoff_s);
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

  // 有数据时快速轮询保持低延迟, 空闲时短暂休眠省 CPU
  if (ws_client_ && ws_client_->state() == nova::ws::WS_STATE_OPEN) {
    if (last_data_ns_ > 0 && (NowNs() - last_data_ns_) < 500'000'000LL) {
      std::this_thread::yield();
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
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
  // 跳过 Kraken v2 订阅确认 (method/success)
  if ((exchange == "krk" || exchange == "kraken") && data.is_object()) {
    if (data.contains("method") || data.contains("success")) return;
  }
  // 跳过 Coinbase 订阅确认 / 心跳
  if ((exchange == "cb" || exchange == "coinbase") &&
      data.value("type", "") == "subscriptions")
    return;

  last_data_ns_ = NowNs(); // 更新心跳时间

  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;


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
    // 无精确匹配订阅 → 数据对策略无用, 不 dispatch
    (void)size;
  };

  // Kraken v2: {"channel":"trade/book/ticker","type":"snapshot/update","data":[...]}
  if ((exchange == "krk" || exchange == "kraken") && data.is_object()) {
    if (data.contains("method") || data.contains("success")) return;
    std::string ch = data.value("channel", "");
    if (ch.empty() || !data.contains("data") || !data["data"].is_array()) return;
    const auto &items = data["data"];

    // 通用 symbol 匹配 (v2 "BTC/USD" vs v1 "XBT/USD" 等格式)
    auto find_inst = [&](const std::string &sym) {
      // 直接匹配
      auto it = symbol_to_inst_.find(sym);
      if (it != symbol_to_inst_.end()) return it;
      // 去斜杠小写匹配
      std::string flat = sym;
      flat.erase(std::remove(flat.begin(), flat.end(), '/'), flat.end());
      std::transform(flat.begin(), flat.end(), flat.begin(), ::tolower);
      for (auto &[k, v] : symbol_to_inst_) {
        std::string kl = k;
        kl.erase(std::remove(kl.begin(), kl.end(), '/'), kl.end());
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        if (kl == flat) return symbol_to_inst_.find(k);
      }
      return symbol_to_inst_.end();
    };

    if (ch == "trade") {
      for (auto &d : items) {
        auto it = find_inst(d.value("symbol", ""));
        if (it == symbol_to_inst_.end()) continue;
        if (!d.contains("trades") || !d["trades"].is_array()) continue;
        for (auto &t : d["trades"]) {
          if (!t.is_object()) continue;
          NovaCoinTrade tr{};
          tr.instrument_id = it->second;
          tr.price = t["price"].is_number() ? t["price"].get<double>()
                     : std::stod(t["price"].get<std::string>());
          tr.qty = t["qty"].is_number() ? t["qty"].get<double>()
                   : std::stod(t["qty"].get<std::string>());
          tr.side = (t.value("side", "") == "buy") ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
          tr.local_time = local_ns;
          tr.local_ns = local_ns;
          dispatch(NOVA_COIN_QUOTE_TRADE, &tr, sizeof(tr));
        }
      }
      return;
    }

    if (ch == "ticker") {
      for (auto &d : items) {
        auto it = find_inst(d.value("symbol", ""));
        if (it == symbol_to_inst_.end()) continue;
        NovaCoinBBO bbo{};
        bbo.instrument_id = it->second;
        bbo.bid_price = d["bid"].is_number() ? d["bid"].get<double>() : std::stod(d["bid"].get<std::string>());
        bbo.ask_price = d["ask"].is_number() ? d["ask"].get<double>() : std::stod(d["ask"].get<std::string>());
        bbo.bid_qty = d["bid_qty"].is_number() ? d["bid_qty"].get<double>() : std::stod(d["bid_qty"].get<std::string>());
        bbo.ask_qty = d["ask_qty"].is_number() ? d["ask_qty"].get<double>() : std::stod(d["ask_qty"].get<std::string>());
        bbo.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      }
      return;
    }

    if (ch == "book") {
      for (auto &d : items) {
        auto it = find_inst(d.value("symbol", ""));
        if (it == symbol_to_inst_.end()) continue;
        NovaCoinDepth depth{};
        depth.instrument_id = it->second;
        depth.local_ns = local_ns;
        auto fill = [&](const std::string &side, const nlohmann::json &arr) {
          int i = 0;
          for (auto &lv : arr) {
            if (i >= NovaCoinDepth::MAX_PRICE_LEVEL || !lv.is_object()) break;
            double px = lv["price"].is_number() ? lv["price"].get<double>() : std::stod(lv["price"].get<std::string>());
            double qty = lv["qty"].is_number() ? lv["qty"].get<double>() : std::stod(lv["qty"].get<std::string>());
            if (side == "bid") { depth.bid[i].price = px; depth.bid[i].qty = qty; }
            else { depth.ask[i].price = px; depth.ask[i].qty = qty; }
            ++i;
          }
          if (i > depth.ob_level) depth.ob_level = static_cast<uint16_t>(i);
        };
        if (d.contains("bids")) fill("bid", d["bids"]);
        if (d.contains("asks")) fill("ask", d["asks"]);
        dispatch(NOVA_COIN_QUOTE_DEPTH, &depth, sizeof(depth));
      }
      return;
    }
    return;
  }

  // Coinbase Exchange WS: {"type":"ticker"/"match"/"snapshot"/"l2update",...}
  if (exchange == "cb" || exchange == "coinbase") {
    std::string etype = data.value("type", "");
    std::string pair = data.value("product_id", "");
    if (pair.empty()) return;

    // match → trade
    if (etype == "match" && inst_id.Valid()) {
      NovaCoinTrade tr{};
      tr.instrument_id = inst_id;
      tr.price = std::stod(data.value("price", "0"));
      tr.qty = std::stod(data.value("size", "0"));
      tr.side = (data.value("side", "") == "buy") ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
      tr.local_time = local_ns;
      tr.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_TRADE, &tr, sizeof(tr));
      return;
    }

    // ticker → BBO
    if (etype == "ticker" && inst_id.Valid()) {
      NovaCoinBBO bbo{};
      bbo.instrument_id = inst_id;
      bbo.bid_price = std::stod(data.value("best_bid", "0"));
      bbo.bid_qty = 0;
      bbo.ask_price = std::stod(data.value("best_ask", "0"));
      bbo.ask_qty = 0;
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      return;
    }

    // snapshot / l2update → 维护本地簿 + BBO dispatch
    if (etype == "snapshot" || etype == "l2update") {
      auto &[bids, asks] = cb_book_[pair];
      if (etype == "snapshot") { bids.clear(); asks.clear(); }
      auto apply = [&](const std::string &side, const nlohmann::json &arr) {
        for (auto &v : arr) {
          if (!v.is_array() || v.size() < 2) continue;
          double px = std::stod(v[0].is_string() ? v[0].get<std::string>() : std::to_string(v[0].get<double>()));
          double qty = std::stod(v[1].is_string() ? v[1].get<std::string>() : std::to_string(v[1].get<double>()));
          if (side == "buy") { if (qty<=0) bids.erase(px); else bids[px]=qty; }
          else { if (qty<=0) asks.erase(px); else asks[px]=qty; }
        }
      };
      std::string key = (etype == "snapshot") ? "bids" : "changes";
      if (data.contains(key)) {
        auto &arr = data[key];
        if (arr.is_array() && !arr.empty())
          apply("buy", arr);  // for snapshot, bids array
      }
      if (etype == "snapshot" && data.contains("asks"))
        apply("sell", data["asks"]);
      else if (etype == "l2update" && data.contains("changes"))
        apply("sell", data["changes"]);  // re-apply with side detection

      // 保留 25 档
      constexpr int kMax = 25;
      while ((int)bids.size() > kMax) bids.erase(std::prev(bids.end()));
      while ((int)asks.size() > kMax) asks.erase(std::prev(asks.end()));
      if (!bids.empty() && !asks.empty() && inst_id.Valid()) {
        NovaCoinBBO bbo{};
        bbo.instrument_id = inst_id;
        bbo.bid_price = bids.begin()->first;
        bbo.ask_price = asks.begin()->first;
        bbo.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      }
      return;
    }

    // 其他 Coinbase 消息 (subscriptions, heartbeat 等)
    return;
  }

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

  // Kraken spread: [channelID, ["bid","ask","ts","bid_vol","ask_vol"], "spread", "pair"]
  if (data.is_array() && data.size() >= 4 && data[1].is_array() &&
      data[2].is_string() && data[2].get<std::string>() == "spread") {
    const auto &s = data[1];
    if (s.size() >= 5 && inst_id.Valid()) {
      NovaCoinBBO bbo{};
      bbo.instrument_id = inst_id;
      bbo.bid_price = std::stod(s[0].get<std::string>());
      bbo.ask_price = std::stod(s[1].get<std::string>());
      bbo.bid_qty = std::stod(s[3].get<std::string>());
      bbo.ask_qty = std::stod(s[4].get<std::string>());
      bbo.update_time = local_ns;
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
    }
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
      auto &[bids, asks] = local_book_[inst_id.key()];
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
      auto &[bids, asks] = local_book_[inst_id.key()];
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

  // OKX / Gate.io 格式
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
    if (ch == "trades") {
      for (auto &t : items) {
        if (!t.is_object()) continue;
        NovaCoinTrade trade{};
        if (inst_id.Valid()) trade.instrument_id = inst_id;
        trade.price = t["px"].is_string()
                          ? std::stod(t["px"].get<std::string>())
                          : t["px"].get<double>();
        trade.qty = t["sz"].is_string()
                        ? std::stod(t["sz"].get<std::string>())
                        : t["sz"].get<double>();
        std::string side = t.value("side", "");
        trade.side = (side == "buy") ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
        trade.local_time = t["ts"].is_string()
                               ? static_cast<int64_t>(std::stod(t["ts"].get<std::string>()) * 1'000'000)
                               : static_cast<int64_t>(t["ts"].get<int64_t>() * 1'000'000);
        trade.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_TRADE, &trade, sizeof(trade));
      }
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

  // Gate.io JSON-RPC 格式
  if (data.contains("method") && data.contains("params") &&
      data["params"].is_array()) {
    std::string method = data["method"].get<std::string>();
    const auto &params = data["params"];

    if (method == "depth.update" && params.size() >= 3) {
      if (!params[1].is_object()) return;
      const auto &d = params[1];
      if (d.contains("bids") && d["bids"].is_array() && !d["bids"].empty() &&
          d.contains("asks") && d["asks"].is_array() && !d["asks"].empty()) {
        NovaCoinBBO bbo{};
        if (inst_id.Valid()) bbo.instrument_id = inst_id;
        auto get_px = [](const nlohmann::json &v) -> double {
          return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
        };
        bbo.bid_price = get_px(d["bids"][0][0]);
        bbo.bid_qty = get_px(d["bids"][0][1]);
        bbo.ask_price = get_px(d["asks"][0][0]);
        bbo.ask_qty = get_px(d["asks"][0][1]);
        bbo.update_time =
            static_cast<int64_t>(d.value("update", 0.0) * 1'000'000'000);
        bbo.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      }
      return;
    }

    if (method == "trades.update" && params.size() >= 2 &&
        params[1].is_array()) {
      for (auto &t : params[1]) {
        if (!t.is_object()) continue;
        NovaCoinTrade trade{};
        if (inst_id.Valid()) trade.instrument_id = inst_id;
        trade.price = t["price"].is_string()
                          ? std::stod(t["price"].get<std::string>())
                          : t["price"].get<double>();
        trade.qty = t["amount"].is_string()
                        ? std::stod(t["amount"].get<std::string>())
                        : t["amount"].get<double>();
        std::string side = t.value("type", "sell");
        trade.side = (side == "buy") ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
        trade.local_time =
            static_cast<int64_t>(t.value("time", 0.0) * 1'000'000'000);
        trade.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_TRADE, &trade, sizeof(trade));
      }
      return;
    }
    // subscribe 确认或非行情消息跳过
    return;
  }

  // MEXC 格式
  if (data.contains("c") && data.contains("d")) {
    std::string ch = data["c"].get<std::string>();
    const auto &d = data["d"];
    if (ch.find("bookTicker") != std::string::npos && d.is_object()) {
      NovaCoinBBO bbo{};
      if (inst_id.Valid()) bbo.instrument_id = inst_id;
      auto get_num = [&](const char *key) -> double {
        if (!d.contains(key)) return 0;
        auto &v = d[key];
        return v.is_string() ? std::stod(v.get<std::string>()) : v.get<double>();
      };
      bbo.bid_price = get_num("b");
      bbo.bid_qty = get_num("B");
      bbo.ask_price = get_num("a");
      bbo.ask_qty = get_num("A");
      bbo.update_time = local_ns;
      bbo.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
      return;
    }
    if (ch.find("deals") != std::string::npos && d.is_object() && d.contains("deals")) {
      for (auto &t : d["deals"]) {
        if (!t.is_object()) continue;
        NovaCoinTrade trade{};
        if (inst_id.Valid()) trade.instrument_id = inst_id;
        trade.price = t["p"].is_string()
                          ? std::stod(t["p"].get<std::string>())
                          : t["p"].get<double>();
        trade.qty = t["v"].is_string()
                        ? std::stod(t["v"].get<std::string>())
                        : t["v"].get<double>();
        int side_int = t.value("S", 0);
        trade.side = (side_int == 1) ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
        trade.local_time = t.contains("t")
            ? static_cast<int64_t>(t["t"].get<int64_t>() * 1'000'000)
            : local_ns;
        trade.local_ns = local_ns;
        dispatch(NOVA_COIN_QUOTE_TRADE, &trade, sizeof(trade));
      }
      return;
    }
  }
}

void WSFeed::ProcessMexcProto(const char *data, size_t len) {
  PushDataV3ApiWrapper wrapper;
  if (!wrapper.ParseFromArray(data, static_cast<int>(len))) {
    WARNING_FLOG("[WSFeed] mexc proto parse failed, {} bytes", len);
    return;
  }

  last_data_ns_ = NowNs();

  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;

  int64_t local_ns = NowNs();
  std::string channel = wrapper.channel();
  std::string raw_sym = wrapper.has_symbol() ? wrapper.symbol() : "";

  // symbol → InstrumentId
  InstrumentId inst_id{};
  if (!raw_sym.empty() && !symbol_to_inst_.empty()) {
    auto it = symbol_to_inst_.find(raw_sym);
    if (it != symbol_to_inst_.end()) {
      inst_id = it->second;
    } else {
      std::string lower = raw_sym;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      for (auto &[key, val] : symbol_to_inst_) {
        std::string kl = key;
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        if (lower == kl) { inst_id = val; break; }
      }
    }
  }

  // dispatch helper
  static std::unordered_map<std::string, int> dispatch_cnt_mx;
  auto dispatch = [&](NOVA_COIN_QUOTE_TYPE qtype, const void *ptr, size_t size) {
    for (size_t i = 0; i < state.subs.size(); ++i) {
      if (state.subs[i].quote_type == qtype &&
          state.subs[i].position &&
          state.subs[i].position->instrument.key() == inst_id.key()) {
        di_mgr->Push(i, ptr, local_ns);
        if (state.subs[i].trigger && state.strategy) {
          if (++dispatch_cnt_mx[channel] <= 3)
            INFO_FLOG("[WSFeed] dispatch mexc qtype={} di={} inst={}", (int)qtype, i, raw_sym);
          state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                       state.subs[i].position);
        }
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
    (void)size;
  };

  // BBO (bookTicker)
  if (channel.find("bookTicker") != std::string::npos &&
      wrapper.has_publicaggrebookticker()) {
    const auto &bt = wrapper.publicaggrebookticker();
    NovaCoinBBO bbo{};
    if (inst_id.Valid()) bbo.instrument_id = inst_id;
    bbo.bid_price = std::stod(bt.bidprice());
    bbo.bid_qty = std::stod(bt.bidquantity());
    bbo.ask_price = std::stod(bt.askprice());
    bbo.ask_qty = std::stod(bt.askquantity());
    bbo.update_time = wrapper.has_sendtime()
                          ? wrapper.sendtime() * 1'000'000
                          : local_ns;
    bbo.local_ns = local_ns;
    dispatch(NOVA_COIN_QUOTE_BBO, &bbo, sizeof(bbo));
    return;
  }

  // Trades (deals)
  if (channel.find("deals") != std::string::npos &&
      wrapper.has_publicaggredeals()) {
    const auto &deals = wrapper.publicaggredeals();
    for (auto &item : deals.deals()) {
      NovaCoinTrade trade{};
      if (inst_id.Valid()) trade.instrument_id = inst_id;
      trade.price = std::stod(item.price());
      trade.qty = std::stod(item.quantity());
      trade.side = (item.tradetype() == 1) ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
      trade.local_time = item.time() * 1'000'000;
      trade.local_ns = local_ns;
      dispatch(NOVA_COIN_QUOTE_TRADE, &trade, sizeof(trade));
    }
    return;
  }
}

END_NOVA_NAMESPACE(trade)
