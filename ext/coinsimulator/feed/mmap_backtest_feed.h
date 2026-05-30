#pragma once

#include "feed/feed_engine.h"

#include "nova_api_quote_struct.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

BEGIN_NOVA_NAMESPACE(trade)

/**
 * MmapBacktestFeed — 多文件 mmap 归并回测 Feed。
 *
 * 扫描目录下所有 .bin 文件, 按时间戳 k-way merge 播放。
 * 零拷贝遍历 record, 比 CSV 解析快 100 倍以上。
 *
 * 目录格式: <root>/<exchange>/backtest/<symbol>/<date>.bin
 *
 * 二进制格式 (每条 record):
 *   [type:1B][ts_ns:8B][inst_id:28B][data:N*8B]
 *   type=1 BBO    = 37 +  40 =  77B
 *   type=2 Depth  = 37 + 170 = 207B
 *   type=3 Trade  = 37 +  25 =  62B
 */
class MmapBacktestFeed : public FeedEngine {
public:
  MmapBacktestFeed();
  ~MmapBacktestFeed() override;

  bool Initialize(const nova::base::Config &cfg) override;
  bool Poll() override;
  void Stop() override;

  void SetDataRoot(const std::string &path) { data_root_ = path; }
  void SetSpeed(double speed) { speed_ = speed; }
  void SetTimeRange(int64_t begin_ns, int64_t end_ns) {
    begin_ns_ = begin_ns;
    end_ns_ = end_ns;
  }

private:
  struct FileReader {
    std::string path;
    int fd = -1;
    void *mmap_addr = nullptr;
    size_t mmap_size = 0;
    const char *cursor = nullptr;
    const char *end = nullptr;
    int64_t next_ts = 0;   // 缓存下一条 record 的时间戳
    uint8_t next_type = 0;

    void close();
    bool open(const std::string &p);
    bool advance();          // 读取当前 cursor 的 ts, 推进 cursor
  };

  void scan_directory(const std::string &root);
  const char *next_record();
  void dispatch_record(const char *rec);
  static int64_t now_ns();

  std::string data_root_;
  std::vector<FileReader> readers_;
  size_t active_count_ = 0;
  double speed_ = 0;
  int64_t begin_ns_ = 0;
  int64_t end_ns_ = 0;
  int64_t start_real_ns_ = 0;
  int64_t start_data_ns_ = 0;
  std::atomic<bool> running_{false};
};

END_NOVA_NAMESPACE(trade)
