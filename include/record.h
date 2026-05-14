#ifndef RECORD_H
#define RECORD_H

#include "common/data.h"
#include <condition_variable>
#include <queue>

namespace record {

struct DataRecord {
  enum class Type {
    ORDER_BOOK,
    TRADE,
    TICKER,
    ORDER_SENT,      // 新增：订单发送
    ORDER_CONFIRMED, // 新增：订单确认
    ORDER_TIMEOUT,   // 新增：订单超时
    MY_ORDER,
    MY_TRADE,
    POSITION
  };

  Type type;
  int64_t timestamp;
  std::string data;

  DataRecord(Type t, int64_t ts, const std::string &d)
      : type(t), timestamp(ts), data(d) {}
};

class Record {
public:
  Record(const std::string &base_dir = "./data/", const std::string &date = "");
  ~Record();

  // 启动保存线程
  void start();
  void stop();

  // 添加数据记录
  void add_order_book(const data::order_book_data &data);
  void add_trade(const data::trade_data &data);
  void add_ticker(const data::ticker_data &data);
  void add_my_order(const data::my_order_data &data);
  void add_my_trade(const data::my_trade_data &data);
  void add_position(const data::my_position_data &data);

  // 新增：双重记录机制
  void add_order_sent(const data::my_order_data &data);    // 记录订单发送
  void add_order_receipt(const data::my_order_data &data); // 记录订单回执

  // 强制刷新到磁盘
  void flush();

private:
  void save_worker();
  void integrity_worker();     // 新增：完整性检查工作线程
  void check_pending_orders(); // 新增：检查待处理订单
  void write_to_file(const std::string &filename, const std::string &content,
                     DataRecord::Type type);
  std::string get_filename(const DataRecord::Type &type);
  void ensure_file_header(const std::string &filename, DataRecord::Type type);
  std::string get_current_date(); // 新增：获取当前日期

  std::string base_dir_;
  std::string fixed_date_; // 新增：回测时的固定日期
  std::queue<DataRecord> data_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::atomic<bool> running_;
  std::thread save_thread_;

  // 新增：完整性检查相关
  std::thread integrity_thread_;
  std::mutex integrity_mutex_;
  std::condition_variable integrity_cv_;

  // 新增：待处理订单管理
  std::unordered_map<std::string, data::my_order_data> pending_orders_;

  // 文件句柄缓存
  std::unordered_map<DataRecord::Type, std::ofstream> file_handles_;
  std::mutex file_mutex_;

  // 已写入表头的文件记录
  std::unordered_set<std::string> headers_written_;
  std::mutex header_mutex_;

  // 批量写入配置
  static constexpr size_t BATCH_SIZE = 100;
  static constexpr int FLUSH_INTERVAL_MS = 1000; // 1秒强制刷新
};

} // namespace record

#endif // RECORD_H
