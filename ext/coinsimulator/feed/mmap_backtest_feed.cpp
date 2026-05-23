#include "feed/mmap_backtest_feed.h"
#include "mock/mock_trade_service.h"
#include "coinrunner_log.h"

#include "nova_api_datainfo.h"
#include "nova_api_instrument.h"
#include "nova_trader_api.h"
#include "base/base_async_log.h"
#include "base/base_config.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

BEGIN_NOVA_NAMESPACE(trade)

namespace {

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Binary record: type(1B) + ts_ns(8B) + inst_id(28B) + data(...)
static constexpr size_t HDR_SIZE = 1 + 8 + 28; // 37
static constexpr uint8_t REC_BBO   = 1;
static constexpr uint8_t REC_DEPTH = 2;
static constexpr uint8_t REC_TRADE = 3;
static constexpr size_t BBO_BODY   = 32;  // bid_px(8) + bid_qty(8) + ask_px(8) + ask_qty(8)
static constexpr size_t DEPTH_BODY = 170;
static constexpr size_t TRADE_BODY = 25;

} // namespace

MmapBacktestFeed::MmapBacktestFeed() {}

MmapBacktestFeed::~MmapBacktestFeed() {
  Stop();
  CloseFile();
}

bool MmapBacktestFeed::Initialize(const nova::base::Config &cfg) {
  (void)cfg;

  // 读取时间范围 (纳秒)
  std::string begin_str, end_str;
  cfg.GetItemValue("Quote.backtest.begin_time", begin_str);
  cfg.GetItemValue("Quote.backtest.end_time", end_str);
  if (!begin_str.empty()) {
    std::string ts = begin_str;
    ts.erase(std::remove(ts.begin(), ts.end(), '.'), ts.end());
    ts.erase(std::remove(ts.begin(), ts.end(), ':'), ts.end());
    if (ts.size() >= 8) {
      try { begin_ns_ = std::stoll(ts + std::string(19 - ts.size(), '0')); }
      catch (...) { begin_ns_ = 0; }
    }
  }
  if (!end_str.empty()) {
    std::string ts = end_str;
    ts.erase(std::remove(ts.begin(), ts.end(), '.'), ts.end());
    ts.erase(std::remove(ts.begin(), ts.end(), ':'), ts.end());
    if (ts.size() >= 8) {
      try { end_ns_ = std::stoll(ts + std::string(19 - ts.size(), '9')); }
      catch (...) { end_ns_ = 0; }
    }
  }

  if (file_paths_.empty()) {
    ERROR_LOG("[MmapFeed] No data files");
    return false;
  }

  return LoadFile();
}

bool MmapBacktestFeed::LoadFile() {
  CloseFile();

  if (file_idx_ >= file_paths_.size()) {
    INFO_LOG("[MmapFeed] All files played");
    return false;
  }

  current_path_ = file_paths_[file_idx_++];
  fd_ = open(current_path_.c_str(), O_RDONLY);
  if (fd_ < 0) {
    ERROR_FLOG("[MmapFeed] Cannot open: {}", current_path_);
    return false;
  }

  struct stat st;
  fstat(fd_, &st);
  mmap_size_ = st.st_size;
  if (mmap_size_ == 0) {
    close(fd_);
    fd_ = -1;
    return false;
  }

  mmap_addr_ = mmap(nullptr, mmap_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (mmap_addr_ == MAP_FAILED) {
    ERROR_FLOG("[MmapFeed] mmap failed for {}", current_path_);
    close(fd_);
    fd_ = -1;
    mmap_addr_ = nullptr;
    mmap_size_ = 0;
    return false;
  }

  cursor_ = static_cast<const char *>(mmap_addr_);
  end_ = cursor_ + mmap_size_;

  INFO_FLOG("[MmapFeed] Loaded {} ({} MB)", current_path_,
            mmap_size_ / 1024 / 1024);
  return true;
}

void MmapBacktestFeed::CloseFile() {
  if (mmap_addr_ && mmap_addr_ != MAP_FAILED) {
    munmap(mmap_addr_, mmap_size_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
  mmap_addr_ = nullptr;
  mmap_size_ = 0;
  cursor_ = nullptr;
  end_ = nullptr;
  fd_ = -1;
}

bool MmapBacktestFeed::Poll() {
  if (!running_) return false;

  // 批量处理: 每次 Poll 处理一批 record (最多 1000 条)
  int batch = 0;
  const char *rec;
  while (batch < 1000 && (rec = NextRecord()) != nullptr) {
    DispatchRecord(rec);
    ++batch;
  }

  if (batch == 0) {
    // 尝试加载下一个文件
    if (LoadFile()) {
      return Poll();
    }
    INFO_LOG("[MmapFeed] Playback complete");
    return false;
  }

  return true;
}

const char *MmapBacktestFeed::NextRecord() {
  while (cursor_ < end_) {
    auto *rec = cursor_;
    uint8_t type = *reinterpret_cast<const uint8_t *>(rec);
    int64_t ts_ns = *reinterpret_cast<const int64_t *>(rec + 1);

    size_t rec_size = HDR_SIZE;
    if (type == REC_BBO)
      rec_size += BBO_BODY;
    else if (type == REC_DEPTH)
      rec_size += DEPTH_BODY;
    else if (type == REC_TRADE)
      rec_size += TRADE_BODY;
    else {
      cursor_ += HDR_SIZE; // skip unknown
      continue;
    }

    cursor_ += rec_size;

    // 时间过滤
    if (begin_ns_ > 0 && ts_ns < begin_ns_) continue;
    if (end_ns_ > 0 && ts_ns > end_ns_) continue;

    // 速度控制
    if (speed_ > 0) {
      if (start_data_ns_ == 0) {
        start_real_ns_ = NowNs();
        start_data_ns_ = ts_ns;
      } else {
        int64_t elapsed_real = NowNs() - start_real_ns_;
        int64_t elapsed_data =
            static_cast<int64_t>((ts_ns - start_data_ns_) / speed_);
        if (elapsed_data > elapsed_real) {
          std::this_thread::sleep_for(
              std::chrono::nanoseconds(elapsed_data - elapsed_real));
        }
      }
    }

    return rec;
  }
  return nullptr;
}

void MmapBacktestFeed::DispatchRecord(const char *rec) {
  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;

  uint8_t type = *reinterpret_cast<const uint8_t *>(rec);
  int64_t ts_ns = *reinterpret_cast<const int64_t *>(rec + 1);
  const char *inst_bytes = rec + 9;
  int64_t local_ns = ts_ns;

  // 构造 InstrumentId
  InstrumentId inst_id{};
  char sym_buf[29]{};
  memcpy(sym_buf, inst_bytes, 28);
  sym_buf[28] = '\0';
  inst_id = InstrumentId::Create(std::string(sym_buf));
  if (!inst_id.Valid()) return;

  auto dispatch = [&](NOVA_COIN_QUOTE_TYPE qtype, const void *ptr) {
    for (size_t i = 0; i < state.subs.size(); ++i) {
      if (state.subs[i].quote_type == qtype) {
        di_mgr->Push(i, ptr, local_ns);
        if (state.subs[i].trigger && state.strategy) {
          state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                       state.subs[i].position);
        }
        return;
      }
    }
  };

  const char *body = rec + HDR_SIZE;

  if (type == REC_BBO) {
    NovaCoinBBO bbo{};
    bbo.instrument_id = inst_id;
    double bid_px, bid_qty, ask_px, ask_qty;
    memcpy(&bid_px, body, 8);
    memcpy(&bid_qty, body + 8, 8);
    memcpy(&ask_px, body + 16, 8);
    memcpy(&ask_qty, body + 24, 8);
    bbo.bid_price = bid_px;
    bbo.bid_qty = bid_qty;
    bbo.ask_price = ask_px;
    bbo.ask_qty = ask_qty;
    bbo.update_time = ts_ns;
    bbo.local_ns = local_ns;
    dispatch(NOVA_COIN_QUOTE_BBO, &bbo);
  } else if (type == REC_DEPTH) {
    NovaCoinDepth depth{};
    depth.instrument_id = inst_id;
    depth.update_time = ts_ns;
    depth.local_ns = local_ns;
    uint16_t ob_level;
    int64_t body_local_ns;
    double bids_px[5], bids_qty[5], asks_px[5], asks_qty[5];
    memcpy(&ob_level, body, 2);
    memcpy(&body_local_ns, body + 2, 8);
    memcpy(bids_px, body + 10, 40);
    memcpy(bids_qty, body + 50, 40);
    memcpy(asks_px, body + 90, 40);
    memcpy(asks_qty, body + 130, 40);
    depth.local_ns = body_local_ns;
    depth.ob_level = ob_level;
    for (int i = 0; i < ob_level && i < 5; ++i) {
      depth.bid[i].price = bids_px[i];
      depth.bid[i].qty = bids_qty[i];
      depth.ask[i].price = asks_px[i];
      depth.ask[i].qty = asks_qty[i];
    }
    dispatch(NOVA_COIN_QUOTE_DEPTH, &depth);
  } else if (type == REC_TRADE) {
    NovaCoinTrade trade{};
    trade.instrument_id = inst_id;
    trade.timestamp = ts_ns;
    double price, qty;
    int8_t side;
    int64_t trade_local_ns;
    memcpy(&price, body, 8);
    memcpy(&qty, body + 8, 8);
    memcpy(&side, body + 16, 1);
    memcpy(&trade_local_ns, body + 17, 8);
    trade.price = price;
    trade.qty = qty;
    trade.side = side == 1 ? NOVA_SIDE_BUY : NOVA_SIDE_SELL;
    trade.local_ns = trade_local_ns;
    dispatch(NOVA_COIN_QUOTE_TRADE, &trade);
  }
}

void MmapBacktestFeed::Stop() { running_ = false; }

END_NOVA_NAMESPACE(trade)
