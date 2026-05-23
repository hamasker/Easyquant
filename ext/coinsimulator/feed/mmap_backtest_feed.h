#pragma once

#include "feed/feed_engine.h"

#include "nova_api_quote_struct.h"

#include <atomic>
#include <cstdint>
#include <string>

BEGIN_NOVA_NAMESPACE(trade)

/**
 * MmapBacktestFeed — 内存映射二进制回测 Feed。
 *
 * 直接 mmap .bin 文件, 零拷贝遍历 record。
 * 比 CSV 解析快 100 倍以上。
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

  void AddDataFile(const std::string &path) { file_paths_.push_back(path); }
  void SetSpeed(double speed) { speed_ = speed; }
  void SetTimeRange(int64_t begin_ns, int64_t end_ns) {
    begin_ns_ = begin_ns;
    end_ns_ = end_ns;
  }

private:
  bool LoadFile();
  void CloseFile();
  const char *NextRecord();
  void DispatchRecord(const char *rec);

  // mmap 相关
  std::string current_path_;
  int fd_ = -1;
  void *mmap_addr_ = nullptr;
  size_t mmap_size_ = 0;
  const char *cursor_ = nullptr;
  const char *end_ = nullptr;

  // 播放控制
  std::vector<std::string> file_paths_;
  size_t file_idx_ = 0;
  double speed_ = 0; // 0 = 全速
  int64_t begin_ns_ = 0;
  int64_t end_ns_ = 0;

  // 时间控制
  int64_t start_real_ns_ = 0;
  int64_t start_data_ns_ = 0;

  std::atomic<bool> running_{false};
};

END_NOVA_NAMESPACE(trade)
