#ifndef DATA_PROCESS_H
#define DATA_PROCESS_H

#include "common/data.h"
#include "configs/configs.h"
#include <condition_variable>

// 前向声明
struct Configs;

namespace DataProcess {
using Depth = NovaCoinDepth;
using DepthLVN = NovaCoinDepthLVN;
using Trade = NovaCoinTrade;
using BBO = NovaCoinBBO;
using Bar = NovaCoinBar;

// 通用交易所判断函数
bool is_main_exchange(const std::string &inst_str,
                      const std::string &aim_exchange);

// 获取交易所的手续费
double get_exchange_taker_fee(const Configs &CFG_, const std::string &exchange);

// 格式化主交易所的USD交易对
std::string format_main_exchange_usd_pair(const std::string &currency_name,
                                          const std::string &aim_exchange);

// 格式化主交易所的NONE交易对
std::string format_main_exchange_none_pair(const std::string &currency_name,
                                           const std::string &aim_exchange);

// 格式化主交易所的交叉交易对
std::string format_main_exchange_cross_pair(const std::string &base_currency,
                                            const std::string &quote_currency,
                                            const std::string &aim_exchange);

// 数据获取和处理函数
bool fetch_data(const nova::quote::DataInfo &one,
                data::InstrumentData &InstData_, bool &flag_data_ready,
                const Configs &CFG_,
                std::unordered_map<std::string, int64_t> *pair_push_cnt = nullptr);

void fetch_data_all(const DataInfoManager *datainfo,
                    data::InstrumentData &InstData_, const Configs &CFG_);

void extract_depth_data(
    const Depth &md, data::depths_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map);

void extract_depth_data(
    const DepthLVN &md, data::depths_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map);

void extract_bbo_data(const BBO &md, data::bbo_data &dd,
                      std::unordered_map<uint16_t, data::delay_data> &delay_map,
                      double alpha_slow = 0.0327868852459,
                      double alpha_fast = 0.5);

void extract_bar_data(
    const Bar &md, data::bar_data &dd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map);

void extract_trade_data(
    const Trade &md, data::depths_data &dd, data::bbo_data &bd,
    std::unordered_map<uint16_t, data::delay_data> &delay_map);

template <int N>
std::array<double, N> trunc_depth_data(const data::depths_data &dd,
                                       const data::depth_type &dtype,
                                       const int8_t &level = 5) {
  bool flag_qty = false;
  int side;
  if (dtype == data::depth_type::BID || dtype == data::depth_type::BID_QUANTITY)
    side = 1;
  else if (dtype == data::depth_type::ASK ||
           dtype == data::depth_type::ASK_QUANTITY)
    side = 0;
  else
    throw std::invalid_argument("invalid depth data type !");

  if (sum_util::EndsWith(data::get_depth_type_name(dtype), "quantity"))
    flag_qty = true;
  std::array<double, N> data{};
  int idx = 0;   // 用于遍历 bids/asks 数组的索引
  int count = 0; // 已收集的有效值数量
  const double threshold = 1e-10;

  while (count < level && idx < 100) {
    double price, quantity;
    if (side == 1) {
      price = dd.bids[idx][0];
      quantity = dd.bids[idx][1];
    } else {
      price = dd.asks[idx][0];
      quantity = dd.asks[idx][1];
    }

    // 同时检查价格和数量都有效
    if (price >= threshold && quantity >= threshold) {
      data[count] = flag_qty ? quantity : price;
      count++;
    }
    idx++;
  }
  return data;
};

std::vector<double> extract_depth_data_vec(const Depth md,
                                           const data::depth_type &dtype,
                                           const int8_t &level);

std::vector<double> extract_depth_data_vec(const Depth md, const int8_t &level,
                                           const data::depth_type &dtype);

template <int N>
std::array<double, N> extract_depth_data_arr(const Depth md,
                                             const data::depth_type &dtype) {
  bool flag_qty = false;
  const NovaCoinPriceLevel *extract_data = nullptr;
  if (dtype == data::depth_type::BID || dtype == data::depth_type::BID_QUANTITY)
    extract_data = md.bid;
  else if (dtype == data::depth_type::ASK ||
           dtype == data::depth_type::ASK_QUANTITY)
    extract_data = md.ask;
  else
    throw std::invalid_argument("invalid depth data type!");
  if (sum_util::EndsWith(data::get_depth_type_name(dtype), "quantity"))
    flag_qty = true;
  std::array<double, N> data;
  const int &ob_num = (md.ob_level > 25) ? 100 : 25;
  const int &for_num = std::min(ob_num, N);
  for (int i = 0; i < for_num; ++i)
    data[i] = flag_qty ? extract_data[i].qty : extract_data[i].price;
  return data;
}

template <int N>
std::array<std::array<double, 2>, N>
extract_depth_data_arrs(const Depth md, const data::depth_type &dtype) {
  const NovaCoinPriceLevel *extract_data = nullptr;
  if (dtype == data::depth_type::BID || dtype == data::depth_type::BID_QUANTITY)
    extract_data = md.bid;
  else if (dtype == data::depth_type::ASK ||
           dtype == data::depth_type::ASK_QUANTITY)
    extract_data = md.ask;
  else
    throw std::invalid_argument("invalid depth data type!");

  std::array<std::array<double, 2>, N> data;
  const int &ob_num = (md.ob_level > 25) ? 100 : 25;
  const int &for_num = std::min(ob_num, N);
  for (int i = 0; i < for_num; ++i) {
    data[i][0] = extract_data[i].price;
    data[i][1] = extract_data[i].qty;
  }
  return data;
}

std::array<double, 2> weighted_price_bbo(const double &ask, const double &askv,
                                         const double &bid, const double &bidv,
                                         const double &tick_size,
                                         const double &taker_fee,
                                         const double &vol_threshold_bid,
                                         const double &vol_threshold_ask);

template <int N>
std::array<double, 2> weighted_price_depth_old(
    const std::array<double, N> &asks, const std::array<double, N> &asksv,
    const std::array<double, N> &bids, const std::array<double, N> &bidsv,
    double tick_size, const double &taker_fee, const double &vol_threshold_bid,
    const double &vol_threshold_ask) {

  constexpr double threshold = 1e-10;

  // 收集有效档位（避免 vector 动态分配）
  std::array<int, N> valid_levels{};
  int valid_count = 0;
  for (int i = 0; i < N; ++i) {
    if (bids[i] > threshold && asks[i] > threshold && bidsv[i] > threshold &&
        asksv[i] > threshold) {
      valid_levels[valid_count++] = i;
    }
  }

  if (valid_count == 0) {
    return {0.0, 0.0};
  }

  auto make_wps_tick = [&](double mid) -> std::array<double, 2> {
    if (!std::isfinite(mid) || mid <= 0.0)
      return {0.0, 0.0};
    const double bid = mid - 0.5 * tick_size;
    const double ask = mid + 0.5 * tick_size;
    // bid 不能 <= 0
    if (bid <= 0.0 || !std::isfinite(bid) || !std::isfinite(ask))
      return {0.0, 0.0};
    return {bid, ask};
  };

  // 只有 1 档：直接按该档位做 fee-adjust 的成交量加权 mid
  if (valid_count == 1) {
    const int idx = valid_levels[0];
    const double A = asks[idx] * (1.0 + taker_fee);
    const double As = asksv[idx];
    const double B = bids[idx] * (1.0 - taker_fee);
    const double Bs = bidsv[idx];
    const double denom = (Bs + As);
    if (!(denom > 0.0))
      return {0.0, 0.0};
    const double mid = (A * Bs + B * As) / denom;
    return make_wps_tick(mid);
  }

  // 计算前两档有效档位的成交量（用于低流动性判别）
  const int idx0 = valid_levels[0];
  const int idx1 = (valid_count > 1) ? valid_levels[1] : idx0;
  const double v2_bid = bidsv[idx0] + bidsv[idx1];
  const double v2_ask = asksv[idx0] + asksv[idx1];

  // A/As/B/Bs 只用前 for_num 个槽位
  std::array<double, N> A{}, As{}, B{}, Bs{};
  int for_num = 0;

  // ========== 分支：量是否足够 ==========
  if (v2_bid < vol_threshold_bid || v2_ask < vol_threshold_ask) {
    // 量不足：用更多有效档位，并按原逻辑做 2 tick shift，但 fee 始终统一乘
    for_num = valid_count;

    const bool bid_low = (v2_bid < vol_threshold_bid);
    const bool ask_low = (v2_ask < vol_threshold_ask);

    for (int j = 0; j < for_num; ++j) {
      const int i = valid_levels[j];

      const double ask_shift = ask_low ? (2.0 * tick_size) : 0.0;
      const double bid_shift = bid_low ? (2.0 * tick_size) : 0.0;

      const double ask_px = asks[i] + ask_shift;
      const double bid_px = bids[i] - bid_shift;

      A[j] = ask_px * (1.0 + taker_fee);
      As[j] = asksv[i];
      B[j] = bid_px * (1.0 - taker_fee);
      Bs[j] = bidsv[i];
    }

  } else {
    // 量足够：只用前两档有效档位（保持你原先“2档”思路）
    for_num = std::min(2, valid_count);

    // 下面这些分支你原代码里有很多“按量关系互换 idx0/idx1”的逻辑；
    // 我保留其精神（在紧价差时按第一档/第二档量关系选用），但统一 fee-adjust。
    // 为了不引入太多复杂度，这里仍然只在前两档里选择 ask_idx / bid_idx。

    const bool tight = (asks[idx0] - bids[idx0] <= tick_size + 1e-10);

    for (int j = 0; j < for_num; ++j) {
      int ask_idx = valid_levels[j];
      int bid_idx = valid_levels[j];

      if (tight && valid_count > 1) {
        // 你原逻辑：当 spread 很紧时，根据一档/二档的量关系决定用哪档的 ask/bid
        const bool ask0_small = (asksv[idx0] < asksv[idx1]);
        const bool bid0_small = (bidsv[idx0] < bidsv[idx1]);

        if (ask0_small && !bid0_small) {
          // ask 更薄，用第二档 ask
          ask_idx = (j == 0) ? idx1 : ask_idx;
        } else if (bid0_small && !ask0_small) {
          // bid 更薄，用第二档 bid
          bid_idx = (j == 0) ? idx1 : bid_idx;
        } else if (bid0_small && ask0_small) {
          // 两边都薄，ask/bid 都用第二档
          ask_idx = (j == 0) ? idx1 : ask_idx;
          bid_idx = (j == 0) ? idx1 : bid_idx;
        }
      }

      const double ask_px = asks[ask_idx];
      const double bid_px = bids[bid_idx];

      A[j] = ask_px * (1.0 + taker_fee);
      As[j] = asksv[ask_idx];
      B[j] = bid_px * (1.0 - taker_fee);
      Bs[j] = bidsv[bid_idx];
    }
  }

  if (for_num <= 0)
    return {0.0, 0.0};

  // ========== 累积成交量 ==========
  std::array<double, N> As_cumsum{};
  std::array<double, N> Bs_cumsum{};
  As_cumsum[0] = As[0];
  Bs_cumsum[0] = Bs[0];
  for (int i = 1; i < for_num; ++i) {
    As_cumsum[i] = As_cumsum[i - 1] + As[i];
    Bs_cumsum[i] = Bs_cumsum[i - 1] + Bs[i];
  }

  // ========== 计算 wp 并求均值 ==========
  double wp_sum = 0.0;
  for (int i = 0; i < for_num; ++i) {
    const double denom = (Bs_cumsum[i] + As_cumsum[i]);
    if (!(denom > 0.0))
      return {0.0, 0.0};
    const double wpi = (A[i] * Bs_cumsum[i] + B[i] * As_cumsum[i]) / denom;
    if (!std::isfinite(wpi))
      return {0.0, 0.0};
    wp_sum += wpi;
  }
  const double wp_mean = wp_sum / double(for_num);
  return make_wps_tick(wp_mean);
}

/**
 * 单档位实时计算加权价格，逻辑与 util.calculate_weighted_prices 一致
 * （不含 EMA，EMA 需由调用方按时间序列维护）
 *
 * @param asks     ask 价格 [level]
 * @param asksv    ask 数量 (usd amount)
 * @param bids     bid 价格 [level]
 * @param bidsv    bid 数量 (usd amount)
 * @param tick_size 最小价格单位
 * @param vol_threshold_bid   bid 方向累计量阈值
 * @param vol_threshold_ask   ask 方向累计量阈值
 * @param eps_thre      有效数据阈值
 * @param wp_invalid_ticks 当无档位达到 vol_threshold 时的 tick 偏移
 * @return {wps_bid, wps_ask} 或 {0, 0} 表示无效
 */
template <int N>
std::array<double, 2> weighted_price_depth(
    const std::array<double, N> &asks, const std::array<double, N> &asksv,
    const std::array<double, N> &bids, const std::array<double, N> &bidsv,
    double tick_size, double vol_threshold_bid, double vol_threshold_ask,
    double eps_thre = 1e-10, double wp_invalid_ticks = 0.0) {
  // 1. 验证所有档位有效
  for (int i = 0; i < N; ++i) {
    if (bids[i] <= eps_thre || asks[i] <= eps_thre || bidsv[i] <= eps_thre ||
        asksv[i] <= eps_thre)
      return {0.0, 0.0};
  }
  // 2. 累积量
  std::array<double, N> cumsum_asksv{}, cumsum_bidsv{};
  cumsum_asksv[0] = asksv[0];
  cumsum_bidsv[0] = bidsv[0];
  for (int i = 1; i < N; ++i) {
    cumsum_asksv[i] = cumsum_asksv[i - 1] + asksv[i];
    cumsum_bidsv[i] = cumsum_bidsv[i - 1] + bidsv[i];
  }
  // 3. 找到第一个达到阈值的档位
  int valid_idx_ask = N - 1;
  int valid_idx_bid = N - 1;
  bool invalid_ask = true;
  bool invalid_bid = true;
  for (int i = 0; i < N; ++i) {
    if (invalid_ask && cumsum_asksv[i] >= vol_threshold_ask) {
      valid_idx_ask = i;
      invalid_ask = false;
    }
    if (invalid_bid && cumsum_bidsv[i] >= vol_threshold_bid) {
      valid_idx_bid = i;
      invalid_bid = false;
    }
  }
  // 4. weighted_bids_tmp[i] = cumsum(bids*bidsv)[:i+1] / cumsum_bids[i]
  std::array<double, N> weighted_bids_tmp{}, weighted_asks_tmp{};
  double sum_bidsw = bidsv[0] * bids[0];
  double sum_asksw = asksv[0] * asks[0];
  double multi_bid = (invalid_bid) ? wp_invalid_ticks : 0.0;
  double multi_ask = (invalid_ask) ? wp_invalid_ticks : 0.0;
  weighted_bids_tmp[0] = sum_bidsw / cumsum_bidsv[0];
  weighted_asks_tmp[0] = sum_asksw / cumsum_asksv[0];
  for (int i = 1; i < valid_idx_ask + 1; ++i) {
    sum_asksw += asksv[i] * asks[i];
    weighted_asks_tmp[i] = sum_asksw / cumsum_asksv[i];
  }
  for (int j = 1; j < valid_idx_bid + 1; ++j) {
    sum_bidsw += bidsv[j] * bids[j];
    weighted_bids_tmp[j] = sum_bidsw / cumsum_bidsv[j];
  }
  double wp_bid = weighted_bids_tmp[valid_idx_bid] - multi_bid * tick_size;
  double wp_ask = weighted_asks_tmp[valid_idx_ask] + multi_ask * tick_size;
  double wp_bidq = cumsum_bidsv[valid_idx_bid];
  double wp_askq = cumsum_asksv[valid_idx_ask];
  double wp = (wp_bid * wp_askq + wp_ask * wp_bidq) / (wp_bidq + wp_askq);
  if (!std::isfinite(wp) || wp <= 0.0)
    return {0.0, 0.0};
  double wps_bid = wp - 0.5 * tick_size;
  double wps_ask = wp + 0.5 * tick_size;
  if (wps_bid <= 0.0 || !std::isfinite(wps_bid) || !std::isfinite(wps_ask))
    return {0.0, 0.0};
  return {wps_bid, wps_ask};
}

double fetch_balance(const data::currency &currency,
                     const ryml::Tree &settings_);

double fetch_credit(const data::currency &currency,
                    const ryml::Tree &settings_);

double fetch_attr(const data::currency &currency, const ryml::Tree &settings_,
                  const std::string &key);

double fetch_max_percentage(const data::currency &currency,
                            const ryml::Tree &settings_);

double get_currency_usd_mp(const std::string &currency_str,
                           const data::InstrumentData &InstData_);

double get_currency_hedge_usdt_mp(const std::string &currency_str,
                                  const data::InstrumentData &InstData_);

void update_pnl(
    data::pnl_data &pnl_data, const data::InstrumentData &InstData_,
    std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_,
    const std::vector<data::currency> &digital_currencies);

void update_hedge_pnl(
    const data::currency currency, const double &position, const double &price,
    std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_);

std::string get_fps_obs_str(
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_);

std::string get_fps_obs_str(
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_,
    const data::InstrumentData &InstData_);

std::string get_fps_obs_str(
    const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_);

std::string get_fps_str(
    const Configs &CFG_, const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_);

std::string get_inst_str(const data::InstrumentData &InstData_,
                         const Configs &CFG_);

std::string get_delay_str(data::InstrumentData &InstData_);

std::string get_hedge_position_str(
    const std::vector<data::currency> &digital_currencies,
    const data::InstrumentData &InstData_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_);

std::string get_balance_str(
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const std::vector<data::currency> &digital_currencies,
    const std::unordered_map<data::currency, data::hedge_position_data>
        &hedge_positions_,
    const std::string &inst_str);

std::string get_volume_str(
    const std::unordered_map<data::currency, data::volume_data> &volumes_,
    const std::string &inst_str);

std::string get_rate_limit_str(const data::InstrumentManager &InstManager_);

void remove_exceed_limit_orders(std::vector<data::order_data> &place_bid_orders,
                                std::vector<data::order_data> &place_ask_orders,
                                const double &rate_limit,
                                const int &flag_balance_base);

std::vector<std::string>
get_currency_matching_insts(const data::currency &currency,
                            const data::InstrumentManager &InstManager_);

void update_orders_cost_sum(
    std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map_,
    const data::InstrumentManager &InstManager_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const Configs &CFG_);

void calculate_cost_transfer_sum(
    std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map_,
    const data::InstrumentManager &InstManager_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verb1);

std::vector<std::string>
load_factor_pool(const std::string &factor_pool_filepath);

double adjust_fair_price(
    const data::InstrumentComponent &IC, const data::currency &currency,
    const double &price, const double &quantity,
    const std::unordered_map<data::currency, data::Setting> &settings_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verbose = false, const double balance_adj = 0.999,
    const double BP = 0.0001);

void update_cuscore(data::cuscore_data &cc, Configs &CFG_, const double &fp_bid,
                    const double &fp_ask, bool verb = false);

void update_ob_volume(
    data::InstrumentData &InstData_,
    std::unordered_map<data::currency, data::fair_price_data> &fps_map_);

std::array<double, 2> fp_forex_IB(const double &fp_bid, const double &fp_ask,
                                  const bool &flag_ib_, const int64_t &ts_tmp,
                                  const std::string &currency_str,
                                  data::bbo_data &ib_data);

bool extract_fps(Configs &CFG_, data::InstrumentData &InstData_,
                 int64_t &global_ts, const std::string &fp_file_path);

void dump_fp(Configs &CFG_, std::ofstream &file_cache_,
             const std::string &inputs, const std::string &date);

int judge_calculate_params(const Configs &CFG_, int64_t &global_ts,
                           data::InstrumentData &InstData_,
                           const std::string &dist_symbol);

void save_calculate_params(const Configs &CFG_, int64_t &global_ts,
                           data::InstrumentData &InstData_,
                           const std::string &dist_symbol);

void cal_and_save_pair_statics(const data::PairStats &pair_stats,
                               const Configs &CFG_,
                               const data::InstrumentData &InstData_,
                               const std::string &dist_symbol);

// 读取ret.csv文件
bool load_ret_csv(Configs &CFG_);

// struct DataSavingParams {
//   std::mutex mut;
//   std::condition_variable cv;
//   bool updated = false;
//   bool stop = false;
//   Configs CFG_;
//   int64_t global_ts;
//   data::InstrumentData InstData_;
//   std::string dist_symbol;

//   void save() {
//     save_calculate_params(CFG_, global_ts, InstData_, dist_symbol);
//   }
// };

void process_balances_monitor(
    std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    const data::depths_data &obs_quote_usd,
    const std::vector<data::currency> &digital_currencies,
    const data::InstrumentComponent &IC, const double &trade_qty,
    const double &avg_price);

void print_fetch_orders_map(
    const std::unordered_map<data::currency, data::fetch_orders_data>
        &fetch_orders_map,
    const data::InstrumentData &InstData_);

// 检查UTC时间是否在周五晚上22点到周日晚上22点之间
bool is_weekend_window_utc(int64_t timestamp_ns);

void calculate_expected_return(data::InstrumentData &InstData_,
                               const Configs &CFG_, bool verbose = false);

inline double envelope_attack_release(double y, double u, double a_rel) {
  return (u >= y) ? u : (y + a_rel * (u - y));
};

void get_orders_margin(
    data::InstrumentData &InstData_, const Configs &CFG_,
    const std::unordered_map<data::currency, data::fair_price_data> &fps_map_,
    const std::unordered_map<data::currency, data::BalanceManager> &BlcMng_,
    bool verbose);

std::vector<data::UniInstID>
get_relative_trading_ids(const data::InstrumentData &InstData_,
                         const data::currency &currency);

} // namespace DataProcess
#endif // DATA_PROCESS_H
