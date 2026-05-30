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
#include <dirent.h>
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
static constexpr size_t BBO_BODY   = 40;
static constexpr size_t DEPTH_BODY = 170;
static constexpr size_t TRADE_BODY = 25;

const std::array<size_t, 4> BODY_SIZE = {0, BBO_BODY, DEPTH_BODY, TRADE_BODY};

} // namespace

// ─── FileReader ───────────────────────────────────────────────────

void MmapBacktestFeed::FileReader::close() {
  if (mmap_addr && mmap_addr != MAP_FAILED) {
    munmap(mmap_addr, mmap_size);
  }
  if (fd >= 0) {
    ::close(fd);
  }
  mmap_addr = nullptr; mmap_size = 0;
  cursor = nullptr; end = nullptr;
  fd = -1; next_ts = 0; next_type = 0;
}

bool MmapBacktestFeed::FileReader::open(const std::string &p) {
  path = p;
  fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) return false;

  struct stat st;
  fstat(fd, &st);
  mmap_size = st.st_size;
  if (mmap_size == 0) { ::close(fd); fd = -1; return false; }

  mmap_addr = mmap(nullptr, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mmap_addr == MAP_FAILED) {
    ::close(fd); fd = -1; mmap_addr = nullptr; mmap_size = 0;
    return false;
  }

  cursor = static_cast<const char *>(mmap_addr);
  end = cursor + mmap_size;
  return advance(); // 预读第一条记录的时间戳
}

bool MmapBacktestFeed::FileReader::advance() {
  while (cursor + HDR_SIZE <= end) {
    next_type = *reinterpret_cast<const uint8_t *>(cursor);
    next_ts = *reinterpret_cast<const int64_t *>(cursor + 1);

    size_t body_len = (next_type >= 1 && next_type <= 3) ? BODY_SIZE[next_type] : 0;
    if (body_len == 0) {
      cursor += HDR_SIZE; // skip unknown
      continue;
    }
    cursor += HDR_SIZE + body_len;
    return true; // 预读到一条有效 record
  }
  // 文件耗尽
  next_ts = INT64_MAX;
  next_type = 0;
  return false;
}

// ─── MmapBacktestFeed ─────────────────────────────────────────────

MmapBacktestFeed::MmapBacktestFeed() { running_ = true; }

MmapBacktestFeed::~MmapBacktestFeed() { Stop(); }

int64_t MmapBacktestFeed::now_ns() { return NowNs(); }

void MmapBacktestFeed::scan_directory(const std::string &root) {
  std::vector<std::string> paths;
  // 只扫描 <root>/<exchange>/backtest/<symbol>/<date>.bin
  std::function<void(const std::string &, int)> dfs =
      [&](const std::string &dir, int depth) {
        if (depth > 5) return;
        DIR *d = opendir(dir.c_str());
        if (!d) return;
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
          if (ent->d_name[0] == '.') continue;
          std::string full = dir + "/" + ent->d_name;
          if (ent->d_type == DT_DIR) {
            dfs(full, depth + 1);
          } else if (ent->d_type == DT_REG || ent->d_type == DT_UNKNOWN) {
            std::string name(ent->d_name);
            if (name.size() > 4 && name.substr(name.size() - 4) == ".bin")
              paths.push_back(full);
          }
        }
        closedir(d);
      };
  dfs(root, 0);

  // 按路径排序保证确定性
  std::sort(paths.begin(), paths.end());

  for (auto &p : paths) {
    FileReader r;
    if (r.open(p)) {
      readers_.push_back(std::move(r));
    }
  }
  active_count_ = readers_.size();
  INFO_FLOG("[MmapFeed] Scanned {} .bin files under {}, {} opened",
            paths.size(), root, active_count_);
}

bool MmapBacktestFeed::Initialize(const nova::base::Config &cfg) {
  (void)cfg;

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

  if (data_root_.empty()) {
    // 默认扫描 /data/bin/ 下所有 .bin
    data_root_ = "/data/bin";
  }

  scan_directory(data_root_);

  if (active_count_ == 0) {
    ERROR_LOG("[MmapFeed] No .bin files found under {}", data_root_);
    return false;
  }

  return true;
}

const char *MmapBacktestFeed::next_record() {
  // 找到 active files 中时间戳最小的
  int best = -1;
  int64_t best_ts = INT64_MAX;

  for (size_t i = 0; i < readers_.size(); ++i) {
    if (readers_[i].next_ts < best_ts) {
      best_ts = readers_[i].next_ts;
      best = static_cast<int>(i);
    }
  }

  if (best < 0 || best_ts == INT64_MAX) return nullptr;

  // 时间过滤
  if (begin_ns_ > 0 && best_ts < begin_ns_) {
    readers_[best].advance();
    if (readers_[best].next_ts == INT64_MAX) --active_count_;
    return next_record(); // 递归跳过
  }
  if (end_ns_ > 0 && best_ts > end_ns_) {
    readers_[best].advance();
    if (readers_[best].next_ts == INT64_MAX) --active_count_;
    return next_record();
  }

  // 速度控制
  if (speed_ > 0) {
    if (start_data_ns_ == 0) {
      start_real_ns_ = NowNs();
      start_data_ns_ = best_ts;
    } else {
      int64_t elapsed_real = NowNs() - start_real_ns_;
      int64_t elapsed_data = static_cast<int64_t>((best_ts - start_data_ns_) / speed_);
      if (elapsed_data > elapsed_real) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds(elapsed_data - elapsed_real));
      }
    }
  }

  // 返回 record 指针 (指回该文件 cursor 的上一条记录)
  // advance() 已经把 cursor 推进到了下一条, 需要回退
  FileReader &r = readers_[best];
  size_t body_len = BODY_SIZE[r.next_type];
  const char *rec = r.cursor - HDR_SIZE - body_len;

  // 推进当前文件
  r.advance();
  if (r.next_ts == INT64_MAX) --active_count_;

  return rec;
}

bool MmapBacktestFeed::Poll() {
  if (!running_) return false;
  if (active_count_ == 0) {
    INFO_LOG("[MmapFeed] Playback complete");
    return false;
  }

  int batch = 0;
  const char *rec;
  while (batch < 1000 && (rec = next_record()) != nullptr) {
    dispatch_record(rec);
    ++batch;
  }

  return active_count_ > 0;
}

void MmapBacktestFeed::Stop() {
  running_ = false;
  for (auto &r : readers_) r.close();
  readers_.clear();
  active_count_ = 0;
}

// ─── dispatch_record (same logic as before) ─────────────────────

void MmapBacktestFeed::dispatch_record(const char *rec) {
  auto &state = MockServiceState::Instance();
  auto *di_mgr = state.data_info_mgr;
  if (!di_mgr) return;

  uint8_t type = *reinterpret_cast<const uint8_t *>(rec);
  int64_t ts_ns = *reinterpret_cast<const int64_t *>(rec + 1);
  const char *inst_bytes = rec + 9;
  int64_t local_ns = ts_ns;

  InstrumentId inst_id{};
  char sym_buf[29]{};
  memcpy(sym_buf, inst_bytes, 28);
  sym_buf[28] = '\0';
  inst_id = InstrumentId::Create(std::string(sym_buf));
  if (!inst_id.Valid()) return;

  auto dispatch = [&](NOVA_COIN_QUOTE_TYPE qtype, const void *ptr) {
    for (size_t i = 0; i < state.subs.size(); ++i) {
      if (state.subs[i].quote_type != qtype) continue;
      if (!state.subs[i].position) continue;
      if (strncmp(state.subs[i].position->instrument.symbol,
                  inst_id.symbol, sizeof(inst_id.symbol)) != 0)
        continue;
      di_mgr->Push(i, ptr, local_ns);
      if (state.subs[i].trigger && state.strategy) {
        state.strategy->on_datainfo(di_mgr, static_cast<int32_t>(i),
                                     state.subs[i].position);
      }
      return;
    }
  };

  const char *body = rec + HDR_SIZE;

  if (type == REC_BBO) {
    NovaCoinBBO bbo{};
    bbo.instrument_id = inst_id;
    double bid_px, bid_qty, ask_px, ask_qty;
    int64_t bbo_local_ns;
    memcpy(&bid_px, body, 8);
    memcpy(&bid_qty, body + 8, 8);
    memcpy(&ask_px, body + 16, 8);
    memcpy(&ask_qty, body + 24, 8);
    memcpy(&bbo_local_ns, body + 32, 8);
    bbo.bid_price = bid_px;
    bbo.bid_qty = bid_qty;
    bbo.ask_price = ask_px;
    bbo.ask_qty = ask_qty;
    bbo.update_time = ts_ns;
    bbo.local_ns = bbo_local_ns;
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
    // DepthLVN 优先 (Kraken aim exchange), 再 Depth
    NovaCoinDepthLVN depth_lvn{};
    depth_lvn.instrument_id = inst_id;
    depth_lvn.update_time = ts_ns;
    depth_lvn.local_time = body_local_ns;
    depth_lvn.local_ns = body_local_ns;
    depth_lvn.ob_level = ob_level;
    int lvn_max = static_cast<int>(NovaCoinDepthLVN::MAX_PRICE_LEVEL);
    for (int i = 0; i < ob_level && i < lvn_max; ++i) {
      depth_lvn.bid[i].price = bids_px[i];
      depth_lvn.bid[i].qty = bids_qty[i];
      depth_lvn.ask[i].price = asks_px[i];
      depth_lvn.ask[i].qty = asks_qty[i];
    }
    dispatch(NOVA_COIN_QUOTE_DEPTH_LVN, &depth_lvn);
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

END_NOVA_NAMESPACE(trade)
