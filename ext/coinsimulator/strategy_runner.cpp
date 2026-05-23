#include "strategy_runner.h"

#include "feed/feed_engine.h"
#include "feed/ws_feed.h"
#include "feed/mmap_backtest_feed.h"
#include "feed/backtest_feed.h"
#include "mock/mock_server.h"
#include "mock/mock_trade_service.h"
#include "mock/mock_trade_engine.h"
#include "mock/kraken_trade_engine.h"
#include "mock/exch_trade_engines.h"
#include "mock/backtest_trade_engine.h"

#include "taking_all_multi.h"
#include "coinrunner_log.h"

#include "nova_api_instrument.h"
#include "nlohmann_json/json.hpp"
#include "base/base_async_log.h"
#include "base/base_config.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <set>
#include <thread>

using namespace nova::trade;
using namespace nova::base;

namespace {

StrategyRunner *g_runner = nullptr;

void SignalHandler(int sig) {
  INFO_FLOG("[Runner] Received signal {}", sig);
  if (g_runner) {
    g_runner->Stop();
  }
}

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// InstrumentId -> 交易所原生 symbol (用于 WebSocket 订阅)
std::string MakeExchangeSymbol(const InstrumentId &inst_id) {
  std::string ticker(inst_id.symbol, inst_id.ticker_len);
  std::string currency = GetCoinCurrencyString(inst_id.currency);
  auto exch = inst_id.exchange;
  auto inst_type = inst_id.inst_type;

  std::string t_lower = ticker;
  std::string c_lower = currency;
  std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::tolower);
  std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::tolower);

  switch (exch) {
  case NOVA_EXCHANGE_BINANCE:
    return t_lower + c_lower;
  case NOVA_EXCHANGE_KRAKE:
    // btc → xbt (唯一特殊转换)
    if (t_lower == "btc") t_lower = "xbt";
    // 长度>3且X/Z前缀 = Kraken API 全名 (XXBT→XBT, ZUSD→USD)
    // 长度≤3 = 内部短名 (eth/xrp/sol), 不 strip
    if (t_lower.size() > 3 && t_lower[0] == 'x')
      t_lower.erase(0, 1);
    if (c_lower.size() > 3 && (c_lower[0] == 'x' || c_lower[0] == 'z'))
      c_lower.erase(0, 1);
    std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::toupper);
    std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::toupper);
    return t_lower + "/" + c_lower;
  case NOVA_EXCHANGE_OK:
    std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::toupper);
    std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::toupper);
    if (inst_type != NOVA_COIN_INST_TYPE_SPOT)
      return t_lower + "-" + c_lower + "-SWAP";
    return t_lower + "-" + c_lower;
  case NOVA_EXCHANGE_COINBASE:
    std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);
    std::transform(currency.begin(), currency.end(), currency.begin(), ::toupper);
    return ticker + "-" + currency;
  case NOVA_EXCHANGE_GT:
    // Gate.io: BASE_QUOTE 大写+下划线
    std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::toupper);
    std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::toupper);
    return t_lower + "_" + c_lower;
  case NOVA_EXCHANGE_MEXC:
    // MEXC: BASEQUOTE 大写+无分隔符
    std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::toupper);
    std::transform(c_lower.begin(), c_lower.end(), c_lower.begin(), ::toupper);
    return t_lower + c_lower;
  default:
    return ticker + currency;
  }
}

std::vector<std::string> DefaultChannelsForExchange(NOVA_EXCHANGE_TYPE exch) {
  switch (exch) {
  case NOVA_EXCHANGE_KRAKE:
    return {"bbo", "depth"};  // Kraken 需要 depth 做 FP
  case NOVA_EXCHANGE_GT:
    return {"bbo", "trade"};  // Gate.io 默认 BBO + trade
  case NOVA_EXCHANGE_MEXC:
    return {"bbo", "trade"};  // MEXC 默认 BBO + trade
  default:
    return {"bbo"};           // 其余默认只要 BBO
  }
}

std::string ExchangeToShortName(NOVA_EXCHANGE_TYPE exch) {
  switch (exch) {
  case NOVA_EXCHANGE_BINANCE: return "bn";
  case NOVA_EXCHANGE_KRAKE:  return "krk";
  case NOVA_EXCHANGE_OK:     return "ok";
  case NOVA_EXCHANGE_COINBASE:return "cb";
  case NOVA_EXCHANGE_GT:     return "gt";
  default: return "unknown";
  }
}

} // namespace

StrategyRunner::StrategyRunner() { g_runner = this; }

StrategyRunner::~StrategyRunner() {
  Stop();
  delete strategy_;
  strategy_ = nullptr;
  for (auto *engine : engines_) {
    delete engine;
  }
  engines_.clear();
  g_runner = nullptr;
}

void StrategyRunner::RegisterEngine(TradeEngine *engine) {
  engines_.push_back(engine);
}

bool StrategyRunner::Initialize() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  INFO_FLOG("[coinrunner] Init: mock framework");
  if (!InitMockFramework()) return false;
  INFO_FLOG("[coinrunner] Init: config");
  if (!InitConfig()) return false;
  INFO_FLOG("[coinrunner] Init: engines");
  if (!InitEngines()) return false;
  INFO_FLOG("[coinrunner] Init: strategy");
  if (!InitStrategy()) return false;
  INFO_FLOG("[coinrunner] Init: feed ({})", mode_);
  if (!InitFeed()) return false;
  INFO_FLOG("[coinrunner] Init complete, entering loop");
  running_ = true;
  return true;
}

bool StrategyRunner::InitConfig() {
  auto *server = GetMockServer();
  if (server && !config_path_.empty()) {
    bool ok = const_cast<Config *>(server->config())->Load(config_path_);
    INFO_FLOG("[coinrunner] Config loaded: {} {}", config_path_,
              ok ? "OK" : "FAIL");

    // 设置日志级别和日志文件
    std::string log_path, screen_lv, file_lv;
    server->config()->GetItemValue("Server.Log.path", log_path);
    server->config()->GetItemValue("Server.Log.screen_level", screen_lv);
    server->config()->GetItemValue("Server.Log.file_level", file_lv);
    if (!screen_lv.empty())
      nova::log::SetScreenLevel(
          static_cast<int>(nova::log::GetLogLevelFromString(screen_lv.c_str())));
    if (!file_lv.empty())
      nova::log::SetFileLevel(
          static_cast<int>(nova::log::GetLogLevelFromString(file_lv.c_str())));
    if (!log_path.empty()) {
      // 在 .log 前插入时间戳
      auto dot = log_path.rfind(".log");
      if (dot != std::string::npos) {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        char ts[20];
        strftime(ts, sizeof(ts), "_%Y%m%d_%H%M%S", localtime(&tt));
        log_path.insert(dot, ts);
      }
      nova::log::SetLogFile(log_path.c_str());
    }

    bool async_log = false;
    server->config()->GetItemValue("Server.Log.async_log", async_log);
    if (async_log) nova::log::SetAsync(true);
  }
  return true;
}

bool StrategyRunner::InitEngines() {
  const auto *cfg = GetMockServer()->config();

  // 解析 Trade.TradeEngine
  picojson::value ary;
  bool has_engine_list = cfg->GetItemValue("Trade.TradeEngine", ary) &&
                         ary.is<picojson::array>();

  if (!has_engine_list || ary.get<picojson::array>().empty()) {
    throw std::runtime_error("Trade.TradeEngine must be a non-empty array");
  }

  bool any_real = false, any_mock = false;
  // engine_name → ws_exch 映射
  static const std::unordered_map<std::string, std::string> name_to_ws = {
      {"BinanceSpotEngine", "bn"}, {"BinanceSwapEngine", "bn_swap"},
      {"KrakenSpotEngine", "krk"}, {"OKXSpotEngine", "ok"},
      {"CoinbaseSpotEngine", "cb"}};

  for (auto &item : ary.get<picojson::array>()) {
    std::string name = item.contains("name") ? item.get("name").to_str() : "";
    int delay_ms = item.contains("match_delay_ms") ? (int)item.get("match_delay_ms").get<double>() : 0;
    int fill_ratio = item.contains("match_fill_ratio") ? (int)item.get("match_fill_ratio").get<double>() : 100;

    // 提取 WS 配置
    if (item.contains("ws_front_address")) {
      auto ws_it = name_to_ws.find(name);
      if (ws_it != name_to_ws.end()) {
        auto &info = ws_configs_[ws_it->second];
        info.ws_url = item.get("ws_front_address").to_str();
        info.ws_core = item.contains("ws_core") ? (int)item.get("ws_core").get<double>() : -1;
      }
    }

    if (name == "MockEngine") {
      any_mock = true;
    } else if (name == "KrakenSpotEngine" || name == "BinanceSpotEngine" ||
               name == "BinanceSwapEngine" || name == "OKXSpotEngine" ||
               name == "CoinbaseSpotEngine") {
      any_real = true;
    } else {
      throw std::runtime_error("Unknown TradeEngine name: " + name);
    }

    TradeEngine *e = nullptr;
    const char *api_section = nullptr;
    if (name == "MockEngine") {
      e = new MockTradeEngine();
    } else if (name == "KrakenSpotEngine") {
      e = new KrakenTradeEngine(); api_section = "kraken";
    } else if (name == "BinanceSpotEngine") {
      e = new BinanceTradeEngine(); api_section = "binance";
    } else if (name == "BinanceSwapEngine") {
      e = new BinanceSwapTradeEngine(); api_section = "binance_u";
    } else if (name == "OKXSpotEngine") {
      e = new OKXTradeEngine(); api_section = "okex";
    } else if (name == "CoinbaseSpotEngine") {
      e = new CoinbaseTradeEngine(); api_section = "coinbase";
    }

    if (e) {
      // 加载 API key + front_address + rate limit
      if (api_section) {
        auto *rest = dynamic_cast<RestTradeEngine *>(e);
        if (rest) {
          rest->LoadApiKeys(*cfg, api_section);
          if (item.contains("front_address"))
            rest->SetApiHost(item.get("front_address").to_str());
          if (item.contains("max_limit_rate")) {
            int max_rate = (int)item.get("max_limit_rate").get<double>();
            double decay = item.contains("decay_rate_per_sec")
                               ? item.get("decay_rate_per_sec").get<double>()
                               : max_rate;
            rest->SetRateLimit(max_rate, decay);
          }
        }
      }
      // 设置撮合参数 + record_dir
      auto *mock = dynamic_cast<MockTradeEngine *>(e);
      if (mock) {
        mock->SetMatchDelayMs(delay_ms);
        mock->SetMatchFillRatio(fill_ratio);
        if (item.contains("record_dir")) {
          auto dir = item.get("record_dir").to_str();
          if (!dir.empty()) mock->SetRecordDir(dir);
        }
      }
      RegisterEngine(e);
    }
  }

  // 模式检测 + 一致性校验
  if (any_real && any_mock) {
    throw std::runtime_error(
        "Cannot mix real engines (KrakenSpotEngine/etc) with MockEngine");
  }

  if (any_real) {
    // prod 模式: 必须有 Quote.<exchange>.enabled
    mode_ = "prod";
    INFO_FLOG("[Runner] Mode: prod (real trade engines)");
  } else {
    // 全是 MockEngine → 检查 Quote 判断 mock 还是 backtest
    bool has_quote = false, has_backtest = false;
    static const char *exch_sections[] = {"binance", "binance_u", "kraken",
                                          "okex", "coinbase", "gateio", "mexc"};
    for (auto *sec : exch_sections) {
      bool en = false;
      cfg->GetItemValue((std::string("Quote.") + sec + ".enabled").c_str(), en);
      if (en) has_quote = true;
    }
    std::string backtest_begin;
    cfg->GetItemValue("Quote.backtest.begin_time", backtest_begin);
    has_backtest = !backtest_begin.empty();

    if (has_backtest && has_quote) {
      throw std::runtime_error(
          "Cannot have both Quote.<exchange>.enabled (mock mode) and "
          "Quote.backtest (backtest mode)");
    }
    if (!has_backtest && !has_quote) {
      throw std::runtime_error(
          "Must specify either Quote.<exchange>.enabled (mock) or "
          "Quote.backtest (backtest)");
    }
    mode_ = has_backtest ? "backtest" : "mock";
    INFO_FLOG("[Runner] Mode: {} (MockEngine)", mode_);
  }

  // 注册引擎
  for (auto *engine : engines_) RegisterMockEngine(engine);
  auto &state = MockServiceState::Instance();
  state.engines = engines_;

  auto *server = GetMockServer();
  if (!server->Initialize()) {
    ERROR_LOG("[Runner] Framework init failed");
    return false;
  }
  INFO_FLOG("[Runner] Engines initialized");
  return true;
}

bool StrategyRunner::InitMockFramework() {
  nova::trade::InitMockFramework();
  return true;
}

bool StrategyRunner::InitStrategy() {
  INFO_FLOG("[coinrunner] Creating TakingDemo...");
  auto *strategy = new TakingDemo();
  strategy_ = strategy;
  MockServiceState::Instance().strategy = strategy;

  INFO_FLOG("[coinrunner] Calling on_init...");
  const auto *cfg = GetMockServer()->config();
  if (!strategy_->on_init(cfg)) {
    ERROR_FLOG("[coinrunner] on_init FAILED");
    return false;
  }
  INFO_FLOG("[coinrunner] Strategy init OK");

  // 给所有 MockTradeEngine 设置 strategy 回调指针
  for (auto *engine : engines_) {
    auto *mock_engine = dynamic_cast<MockTradeEngine *>(engine);
    if (mock_engine) mock_engine->SetStrategy(strategy_);
  }

  return true;
}

bool StrategyRunner::InitFeed() {
  // backtest: mmap 二进制文件回放
  if (mode_ == "backtest") {
    auto *bf = new MmapBacktestFeed();
    // 从 -d 参数或 Quote.backtest 读取数据源
    if (!data_dir_.empty()) {
      bf->AddDataFile(data_dir_);
    } else {
      const auto *cfg = GetMockServer()->config();
      picojson::value input_dirs;
      if (cfg->GetItemValue("Quote.backtest.input_dirs", input_dirs)) {
        // 从 input_dirs 展开路径 (简化: 用 -d 传具体文件)
        WARNING_LOG("[Runner] Use -d <file.bin> for backtest data");
      }
    }
    bf->SetSpeed(0);
    feeds_.emplace_back(bf);
    const auto *cfg = GetMockServer()->config();
    if (!bf->Initialize(*cfg)) {
      ERROR_LOG("[Runner] Backtest feed init failed");
      return false;
    }
    INFO_FLOG("[Runner] Feed engine (mmap backtest) initialized");
    return true;
  }

  // mock/prod 都从 Quote 配置驱动数据源
  if (!InitProdFeeds()) return false;

  INFO_FLOG("[Runner] Feed engine ({}) initialized with {} feed(s)", mode_,
            feeds_.size());
  return true;
}

bool StrategyRunner::InitProdFeeds() {
  INFO_LOG("[Runner] Creating prod feeds from config...");
  const auto *cfg = GetMockServer()->config();

  auto &state = MockServiceState::Instance();
  auto &subs = state.subs;

  // 交易所配置: {config_section, exchange, inst_type, ws_name}
  struct QuoteExchConfig {
    const char *section;
    NOVA_EXCHANGE_TYPE exch;
    NOVA_COIN_INST_TYPE inst_type;
    const char *ws_exch;
  };
  static const std::vector<QuoteExchConfig> exch_configs = {
      {"binance", NOVA_EXCHANGE_BINANCE, NOVA_COIN_INST_TYPE_SPOT, "bn"},
      {"binance_u", NOVA_EXCHANGE_BINANCE, NOVA_COIN_INST_TYPE_SWAP, "bn_swap"},
      {"kraken", NOVA_EXCHANGE_KRAKE, NOVA_COIN_INST_TYPE_SPOT, "krk"},
      {"okex", NOVA_EXCHANGE_OK, NOVA_COIN_INST_TYPE_SPOT, "ok"},
      {"coinbase", NOVA_EXCHANGE_COINBASE, NOVA_COIN_INST_TYPE_SPOT, "cb"},
    {"gateio", NOVA_EXCHANGE_GT, NOVA_COIN_INST_TYPE_SPOT, "gt"},
    {"mexc", NOVA_EXCHANGE_MEXC, NOVA_COIN_INST_TYPE_SPOT, "mexc"},
  };

  for (auto &ec : exch_configs) {
    // 检查 Quote.<section>.enabled
    std::string prefix = std::string("Quote.") + ec.section + ".";
    bool enabled = false;
    cfg->GetItemValue(prefix + "enabled", enabled);
    if (!enabled) continue;

    // 读取 channels (JSON 数组)
    std::vector<std::string> channels;
    picojson::value ch_ary;
    if (cfg->GetItemValue(prefix + "channels", ch_ary) &&
        ch_ary.is<picojson::array>()) {
      for (auto &v : ch_ary.get<picojson::array>()) {
        if (v.is<std::string>()) channels.push_back(v.get<std::string>());
      }
    }
    if (channels.empty()) {
      channels = DefaultChannelsForExchange(ec.exch);
    }

    // 过滤策略订阅
    struct {
      std::set<std::string> symbol_set;
      std::vector<std::string> symbols;
      std::unordered_map<std::string, InstrumentId> mapping;
    } info;

    for (auto &sub : subs) {
      if (!sub.position) continue;
      auto inst_id = sub.position->instrument;
      if (!inst_id.Valid()) continue;
      if (inst_id.exchange != ec.exch) continue;
      if (inst_id.inst_type != ec.inst_type) continue;

      std::string exch_sym = MakeExchangeSymbol(inst_id);
      if (info.symbol_set.insert(exch_sym).second) {
        info.symbols.push_back(exch_sym);
      }
      info.mapping[exch_sym] = inst_id;
    }

    if (info.symbols.empty()) {
      INFO_FLOG("[Runner] Quote.{} enabled but no matching instruments", ec.section);
      continue;
    }

    // 读取 core 配置 (-1 或不填则随机)
    int core = -1;
    cfg->GetItemValue(prefix + "core", core);

    auto *wf = new WSFeed();
    wf->SetExchange(ec.ws_exch);

    // 从 Trade.TradeEngine 读取 ws_front_address 和 ws_core
    auto ws_it = ws_configs_.find(ec.ws_exch);
    if (ws_it != ws_configs_.end() && !ws_it->second.ws_url.empty()) {
      wf->SetWsUrl(ws_it->second.ws_url);
      core = ws_it->second.ws_core;
    }
    wf->SetCore(core);
    wf->SetSymbols(info.symbols);
    wf->SetChannels(channels);
    wf->SetInstrumentMap(info.mapping);

    INFO_FLOG("[Runner] Prod feed: Quote.{} → {} {} symbols",
              ec.section, ec.ws_exch, info.symbols.size());
    for (auto &sym : info.symbols) {
      INFO_FLOG("[Runner]   {} -> {}", ec.ws_exch, sym);
    }

    feeds_.emplace_back(wf);
  }

  if (feeds_.empty()) {
    WARNING_LOG("[Runner] No Quote exchange sections enabled, fallback to mock");
    auto *wf = new WSFeed();
    wf->SetExchange("binance");
    feeds_.emplace_back(wf);
    return feeds_[0]->Initialize(*cfg);
  }

  for (auto &feed : feeds_) {
    if (!feed->Initialize(*cfg)) {
      ERROR_LOG("[Runner] Prod feed initialization failed");
      return false;
    }
  }

  INFO_FLOG("[Runner] Prod feeds initialized: {} feed(s)", feeds_.size());
  return true;
}

void StrategyRunner::Run() {
  if (!running_) {
    ERROR_LOG("[Runner] Not initialized");
    return;
  }

  INFO_FLOG("[Runner] Starting main loop in {} mode with {} feed(s)", mode_,
            feeds_.size());

  // 性能剖析: 每 10s 输出各阶段累计耗时
  uint64_t t_poll = 0, t_onpoll = 0, t_reminder = 0, t_idle = 0;
  uint64_t n_loops = 0, last_report_ns = 0;

  while (running_) {
    uint64_t loop_start = NowNs();

    // ── Feed 轮询 ──
    uint64_t t0 = NowNs();
    bool any_alive = false;
    for (auto &feed : feeds_) {
      if (feed && feed->Poll()) {
        any_alive = true;
      }
    }
    t_poll += NowNs() - t0;

    if (feeds_.empty()) {
      std::this_thread::yield();
      continue;
    }
    if (!any_alive) {
      INFO_LOG("[Runner] All feeds ended, stopping");
      break;
    }

    // ── 策略回调 ──
    t0 = NowNs();
    if (strategy_) {
      strategy_->on_poll(static_cast<int64_t>(loop_start));
    }
    t_onpoll += NowNs() - t0;

    // ── Reminder ──
    t0 = NowNs();
    ProcessReminders(loop_start);
    t_reminder += NowNs() - t0;

    // ── 空闲 ──
    t0 = NowNs();
    std::this_thread::yield();
    t_idle += NowNs() - t0;
    ++n_loops;

    // 每 10s 打印一次剖析报告
    if (last_report_ns == 0) last_report_ns = loop_start;
    if (loop_start - last_report_ns > 10'000'000'000ULL) {
      auto ms = [](uint64_t ns) { return ns / 1'000'000ULL; };
      uint64_t total = t_poll + t_onpoll + t_reminder + t_idle;
      INFO_FLOG("[Profiler] {} loops in 10s | Poll={}ms({:.0f}%) on_poll={}ms({:.0f}%) Reminder={}ms({:.0f}%) Idle={}ms({:.0f}%)",
                n_loops,
                ms(t_poll), total > 0 ? t_poll * 100.0 / total : 0,
                ms(t_onpoll), total > 0 ? t_onpoll * 100.0 / total : 0,
                ms(t_reminder), total > 0 ? t_reminder * 100.0 / total : 0,
                ms(t_idle), total > 0 ? t_idle * 100.0 / total : 0);
      t_poll = t_onpoll = t_reminder = t_idle = 0;
      n_loops = 0;
      last_report_ns = loop_start;
    }
  }

  INFO_LOG("[Runner] Main loop exited");
}

void StrategyRunner::Stop() {
  running_ = false;
  for (auto &feed : feeds_) {
    if (feed) {
      feed->Stop();
    }
  }
}

void StrategyRunner::ProcessReminders(uint64_t cur_ns) {
  auto &state = MockServiceState::Instance();
  std::vector<MockServiceState::ReminderEntry> due;
  {
    std::lock_guard<std::mutex> lk(state.reminder_mutex);
    auto &reminders = state.reminders;
    auto it = reminders.begin();
    while (it != reminders.end()) {
      if (it->nano_time <= cur_ns) {
        due.push_back(*it);
        it = reminders.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto &r : due) {
    if (strategy_) strategy_->on_reminder(r.data, cur_ns);
  }
}
