#include "nvws/ws_websocket_client.h"

#include "nova_api_datainfo.h"
#include "nova_api_instrument.h"
#include "nova_api_quote_struct.h"
#include "nova_api_trade_position.h"
#include "nova_trader_api.h"
#include "nlohmann_json/json.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace nova::ws;
using namespace nova::quote;
using namespace nova::trade;

namespace {

constexpr size_t kBboDiIndex = 0;

int64_t NowNs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

class PrintStrategy final : public StrategyApi {
public:
  bool on_init(const Config *cfg) override {
    (void)cfg;
    return true;
  }

  void on_datainfo(const DataInfoManager *datainfo, int32_t di,
                   const SecurityPosition *position) override {
    const auto &infos = datainfo->datainfo();
    if (di < 0 || static_cast<size_t>(di) >= infos.size()) {
      std::cerr << "[Strategy] invalid DataInfo index: " << di << std::endl;
      return;
    }
    const auto *raw =
        static_cast<const NovaCoinBBO *>(infos[di].buffer().back());
    if (raw == nullptr) {
      std::cerr << "[Strategy] empty DataInfo buffer" << std::endl;
      return;
    }

    std::cout << "[Strategy] " << position->instrument.GetSymbol() << " "
              << "bid=" << raw->bid_price << "@" << raw->bid_qty << " "
              << "ask=" << raw->ask_price << "@" << raw->ask_qty << " "
              << "update_ns=" << raw->update_time
              << " local_ns=" << raw->local_ns << std::endl;
  }
};

struct QuoteContext {
  QuoteContext(const InstrumentId &inst, StrategyApi *sty)
      : instrument_id(inst), position(inst, nullptr, 0.0, 0.0), strategy(sty) {}

  InstrumentId instrument_id;
  DataInfoManager data_info;
  SecurityPosition position;
  StrategyApi *strategy = nullptr;
  std::mutex mutex;
  std::atomic<bool> running{true};
};

class BinanceBookTickerSpi final : public WSClientSpi {
public:
  BinanceBookTickerSpi(QuoteContext &ctx, std::string stream)
      : ctx_(ctx), stream_name_(std::move(stream)) {}

  void Attach(WSClientApi *api) { api_ = api; }

  void OnFail(WS_ERROR_INFO err) override {
    std::cerr << "[WS] connect failed: (" << err.code << ") " << err.msg
              << std::endl;
  }

  void OnOpen(int group) override {
    std::cout << "[WS] connected, group=" << group << std::endl;
    if (!api_) {
      return;
    }
    nlohmann::json sub_req{
        {"method", "SUBSCRIBE"},
        {"params", {stream_name_}},
        {"id", 1},
    };
    auto err = api_->Send(sub_req.dump());
    if (err.code != WS_OK) {
      std::cerr << "[WS] send subscribe failed: (" << err.code << ") "
                << err.msg << std::endl;
    } else {
      std::cout << "[WS] subscribe: " << stream_name_ << std::endl;
    }
  }

  void OnMessage(const char *msg, size_t len, int group) override {
    try {
      const auto payload = nlohmann::json::parse(std::string_view(msg, len));
      if (payload.contains("result") && payload.contains("id")) {
        // subscription ack
        std::cout << "[WS] subscribe ack received" << std::endl;
        return;
      }

      const nlohmann::json *data = nullptr;
      if (payload.contains("data")) {
        data = &payload["data"];
      } else {
        data = &payload;
      }
      if (data == nullptr || !data->contains("b") || !data->contains("a")) {
        return;
      }

      auto parse_decimal = [](const nlohmann::json &value) -> double {
        if (value.is_string()) {
          return std::stod(value.get<std::string>());
        }
        return value.get<double>();
      };

      NovaCoinBBO bbo{};
      bbo.instrument_id = ctx_.instrument_id;
      bbo.bid_price = parse_decimal((*data)["b"]);
      bbo.bid_qty = parse_decimal((*data)["B"]);
      bbo.ask_price = parse_decimal((*data)["a"]);
      bbo.ask_qty = parse_decimal((*data)["A"]);

      const auto exch_ms = data->value("E", int64_t{0});
      bbo.update_time = exch_ms * 1'000'000;
      bbo.local_ns = NowNs();

      {
        std::scoped_lock lk(ctx_.mutex);
        if (!ctx_.data_info.Push(kBboDiIndex, &bbo, bbo.local_ns)) {
          std::cerr << "[Feed] push DataInfo failed" << std::endl;
          return;
        }
        ctx_.strategy->on_datainfo(&ctx_.data_info, static_cast<int32_t>(kBboDiIndex),
                                   &ctx_.position);
      }
    } catch (const std::exception &ex) {
      std::cerr << "[WS] parse error: " << ex.what() << std::endl;
    }
  }

  void OnClose(bool manual) override {
    std::cout << "[WS] connection closed, manual=" << (manual ? "true" : "false")
              << std::endl;
    ctx_.running = false;
  }

private:
  QuoteContext &ctx_;
  std::string stream_name_;
  WSClientApi *api_{nullptr};
};

std::atomic<QuoteContext *> g_ctx{nullptr};

void SignalHandler(int sig) {
  auto *ctx = g_ctx.load();
  if (ctx != nullptr) {
    ctx->running = false;
  }
}

} // namespace

int main(int argc, char **argv) {
  const char *ws_url = "wss://stream.binance.com:9443/ws";
  std::string symbol = "btcusdt"; // Binance symbol (lowercase)
  if (argc > 1) {
    symbol = argv[1];
  }
  const std::string stream = symbol + "@bookTicker";

  InstrumentId inst = InstrumentId::Create("btc_usdt_spot.bn");
  if (!inst.Valid()) {
    std::cerr << "Failed to create InstrumentId" << std::endl;
    return 1;
  }

  PrintStrategy strategy;
  QuoteContext ctx(inst, &strategy);

  DataInfoManager::Param di_param{};
  di_param.trigger_type = DataInfoManager::TriggerType::TRIGGER_TYPE_MASK;
  DataInfoManager::DIParam bbo_param{};
  bbo_param.dip.buf_capacity = 64;
  bbo_param.dip.buf_col_bytes = sizeof(NovaCoinBBO);
  bbo_param.dip.buf_type = NovaCoinBBO::QuoteType();
  bbo_param.dip.overtime = 1'000'000'000; // 1s
  bbo_param.trigger = true;
  di_param.datainfo.emplace_back(bbo_param);
  if (!ctx.data_info.Init(di_param)) {
    std::cerr << "Failed to initialise DataInfoManager" << std::endl;
    return 1;
  }

  BinanceBookTickerSpi spi(ctx, stream);
  WSClientApi *client = WSClientApi::Create(&spi, true, 0);
  spi.Attach(client);

  g_ctx = &ctx;
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  const auto err = client->Initialize(ws_url, -1, 10, false);
  if (err.code != WS_OK) {
    std::cerr << "WS initialise failed: (" << err.code << ") " << err.msg
              << std::endl;
    return 1;
  }

  std::cout << "Streaming " << stream << " from " << ws_url
            << ", press Ctrl+C to exit..." << std::endl;

  while (ctx.running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  client->Stop();
  std::cout << "Exited gracefully." << std::endl;
  return 0;
}


