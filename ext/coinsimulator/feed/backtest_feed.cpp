#include "feed/backtest_feed.h"
#include "mock/mock_trade_service.h"

#include "nova_api_datainfo.h"
#include "nova_api_instrument.h"
#include "nova_trader_api.h"
#include "base/base_async_log.h"

#include <chrono>
#include <sstream>
#include <thread>

BEGIN_NOVA_NAMESPACE(trade)

BacktestFeed::BacktestFeed() {}

BacktestFeed::~BacktestFeed() { Stop(); }

bool BacktestFeed::Initialize(const nova::base::Config &cfg) {
  std::string begin_str, end_str;
  cfg.GetItemValue("Quote.backtest.begin_time", begin_str);
  cfg.GetItemValue("Quote.backtest.end_time", end_str);

  if (!begin_str.empty()) {
    std::string date = begin_str.substr(0, 8);
    begin_ns_ = std::stoll(date + "00000000000");
  }
  if (!end_str.empty()) {
    std::string date = end_str.substr(0, 8);
    end_ns_ = std::stoll(date + "235959999999");
  }

  INFO_FLOG("[BacktestFeed] Init {} files, range [{}, {}], speed {}x",
            file_paths_.size(), begin_ns_, end_ns_, speed_);
  return true;
}

void BacktestFeed::AddDataFile(const std::string &path) {
  file_paths_.push_back(path);
}

void BacktestFeed::SetTimeRange(int64_t begin_ns, int64_t end_ns) {
  begin_ns_ = begin_ns;
  end_ns_ = end_ns;
}

bool BacktestFeed::Poll() {
  if (queue_.empty() && !file_paths_.empty()) {
    for (auto &path : file_paths_) {
      LoadCsvFile(path, queue_);
    }
    file_paths_.clear();

    if (queue_.empty()) {
      INFO_LOG("[BacktestFeed] No events loaded");
      return false;
    }

    start_real_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    start_data_ns_ = queue_.top().timestamp_ns;
    INFO_FLOG("[BacktestFeed] Loaded {} events", queue_.size());
  }

  if (queue_.empty()) {
    INFO_LOG("[BacktestFeed] Playback complete");
    return false;
  }

  auto event = queue_.top();
  queue_.pop();

  if (speed_ > 0 && start_data_ns_ > 0) {
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    int64_t elapsed_real = now_ns - start_real_ns_;
    int64_t elapsed_data =
        static_cast<int64_t>((event.timestamp_ns - start_data_ns_) / speed_);

    if (elapsed_data > elapsed_real) {
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(elapsed_data - elapsed_real));
    }
  }

  DispatchEvent(event);
  return !queue_.empty();
}

void BacktestFeed::Stop() { running_ = false; }

void BacktestFeed::LoadCsvFile(const std::string &path, EventQueue &queue) {
  std::ifstream file(path);
  if (!file.is_open()) {
    ERROR_FLOG("[BacktestFeed] Cannot open: {}", path);
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;

    std::istringstream ss(line);
    std::string type;
    if (!std::getline(ss, type, ',')) continue;

    Event event{};
    if (type == "B") {
      event.quote_type = NOVA_COIN_QUOTE_BBO;
      event.raw_data.resize(sizeof(NovaCoinBBO));
    } else if (type == "D") {
      event.quote_type = NOVA_COIN_QUOTE_DEPTH;
      event.raw_data.resize(sizeof(NovaCoinDepth));
    } else if (type == "X") {
      event.quote_type = NOVA_COIN_QUOTE_TRADE;
      event.raw_data.resize(sizeof(NovaCoinTrade));
    } else {
      continue;
    }

    std::string inst_id_str;
    std::getline(ss, inst_id_str, ',');

    std::string ts_str;
    std::getline(ss, ts_str, ',');
    event.timestamp_ns = std::stoll(ts_str);

    if (begin_ns_ > 0 && event.timestamp_ns < begin_ns_) continue;
    if (end_ns_ > 0 && event.timestamp_ns > end_ns_) continue;

    if (event.quote_type == NOVA_COIN_QUOTE_BBO) {
      auto *bbo = reinterpret_cast<NovaCoinBBO *>(event.raw_data.data());
      new (bbo) NovaCoinBBO();
      bbo->instrument_id = InstrumentId::Create(inst_id_str.c_str());
      bbo->update_time = event.timestamp_ns;

      std::string bp, bq, ap, aq;
      std::getline(ss, bp, ',');
      std::getline(ss, bq, ',');
      std::getline(ss, ap, ',');
      std::getline(ss, aq, ',');
      bbo->bid_price = std::stod(bp);
      bbo->bid_qty = std::stod(bq);
      bbo->ask_price = std::stod(ap);
      bbo->ask_qty = std::stod(aq);
    } else if (event.quote_type == NOVA_COIN_QUOTE_TRADE) {
      auto *trade = reinterpret_cast<NovaCoinTrade *>(event.raw_data.data());
      new (trade) NovaCoinTrade();
      trade->instrument_id = InstrumentId::Create(inst_id_str.c_str());
      trade->timestamp = event.timestamp_ns;

      std::string price, qty, side_str;
      std::getline(ss, price, ',');
      std::getline(ss, qty, ',');
      std::getline(ss, side_str, ',');
      trade->price = std::stod(price);
      trade->qty = std::stod(qty);
      trade->side = (side_str == "sell" || side_str == "0") ? NOVA_SIDE_SELL
                                                             : NOVA_SIDE_BUY;
    } else if (event.quote_type == NOVA_COIN_QUOTE_DEPTH) {
      auto *depth = reinterpret_cast<NovaCoinDepth *>(event.raw_data.data());
      new (depth) NovaCoinDepth();
      depth->instrument_id = InstrumentId::Create(inst_id_str.c_str());
      depth->update_time = event.timestamp_ns;

      for (int i = 0; i < 5; ++i) {
        std::string px, qty;
        std::getline(ss, px, ',');
        std::getline(ss, qty, ',');
        if (!px.empty()) depth->bid[i].price = std::stod(px);
        if (!qty.empty()) depth->bid[i].qty = std::stod(qty);
      }
      for (int i = 0; i < 5; ++i) {
        std::string px, qty;
        std::getline(ss, px, ',');
        std::getline(ss, qty, ',');
        if (!px.empty()) depth->ask[i].price = std::stod(px);
        if (!qty.empty()) depth->ask[i].qty = std::stod(qty);
      }
      depth->ob_level = 5;
    }

    queue.push(std::move(event));
  }
}

void BacktestFeed::DispatchEvent(const Event &event) {
  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;

  for (size_t i = 0; i < state.subs.size(); ++i) {
    if (state.subs[i].quote_type == event.quote_type) {
      auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

      di_mgr->Push(i, event.raw_data.data(), now_ns);
      if (state.subs[i].trigger && state.strategy) {
        state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                     state.subs[i].position);
      }
      break;
    }
  }
}

END_NOVA_NAMESPACE(trade)
