#pragma once

#include "feed/feed_engine.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <queue>

BEGIN_NOVA_NAMESPACE(trade)

class BacktestFeed : public FeedEngine {
public:
  BacktestFeed();
  ~BacktestFeed() override;

  bool Initialize(const nova::base::Config &cfg) override;
  bool Poll() override;
  void Stop() override;

  void AddDataFile(const std::string &path);
  void SetSpeed(double speed) { speed_ = speed; }
  void SetTimeRange(int64_t begin_ns, int64_t end_ns);

private:
  struct Event {
    int64_t timestamp_ns;
    NOVA_COIN_QUOTE_TYPE quote_type;
    std::vector<char> raw_data;

    bool operator>(const Event &other) const {
      return timestamp_ns > other.timestamp_ns;
    }
  };

  using EventQueue =
      std::priority_queue<Event, std::vector<Event>, std::greater<Event>>;

  void LoadCsvFile(const std::string &path, EventQueue &queue);
  void DispatchEvent(const Event &event);

  EventQueue queue_;
  std::vector<std::string> file_paths_;

  int64_t begin_ns_ = 0;
  int64_t end_ns_ = 0;
  int64_t start_real_ns_ = 0;
  int64_t start_data_ns_ = 0;
  double speed_ = 1.0;
  bool running_ = true;
};

END_NOVA_NAMESPACE(trade)
