#pragma once
#include "common/data.h"

namespace digital_fp {

// wp/fp: [0]=bid, [1]=ask, USD计价
struct Quote2 {
  std::array<double, 2> px{{-1.0, -1.0}};
  uint64_t local_ts{0}; // ns
  bool ok{false};
};

// 每个交易所两路信号：depth / bbo
struct Ex2 {
  Quote2 depth;
  Quote2 bbo;
};

struct Params {
  // age 权重：w = exp(-age / tau_age)
  double tau_age_ms{120.0}; // 80~150ms 常用

  // gap 纠偏强度（建议很小，10s半衰对应约 0.0007）
  double kappa_m{0.9};

  // spread 融合：默认强烈建议先 0（完全用 aim 自身 spread）
  double beta_s{0.0};

  // spread 约束（bps），内部用 log-spread 近似
  double s_min_bps{0.1};
  double s_max_bps{250.0};

  // mid gap 截断（bps）
  double g_cap_bps{60.0};

  // 鲁棒剔除：thr = k_mad * MAD
  double k_mad{10.0};

  // 数值稳定
  double eps{1e-12};

  // 交易所权重默认平分：0=bn,1=ok,2=cb
  std::array<double, 3> ex_w{{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}};

  // 通道先验：0=depth,1=bbo
  std::array<double, 2> src_u{{1.0, 0.85}};

  // ===== 噪声方差质量权重（跨通道统一）=====
  double alpha_m{0.02};      // 慢EMA（用于残差）
  double alpha_var{0.05};    // fast var EWMA
  double alpha_base{0.005};  // slow baseline var EWMA（0.001~0.01）
  double var_init_bps{20.0}; // 初始噪声（bps）
  double var_floor{1e-12};   // 方差下限（log尺度）

  // ===== 变化率记忆项（你关注的 lead return）=====
  // 10ms更新、10s半衰：rho≈exp(-ln2*0.01/10)=0.999307
  double rho_z{0.999307};
  // 变化率状态 z 的系数（用回归估更好）
  double gamma_z{0.2};
  // ===== anti-windup（推荐）=====
  // gap门控阈值：|g_m| < g_gate 时逐步压小 dm_ret
  double g_gate_bps{30.0}; // 10~50 bps 常用
  // gap很小就直接把 z 清零（可选，强硬但稳）
  double g_reset_bps{0.0}; // 3~10 bps 常用
  // z 的 clip（bps），防止积分漂移太大
  double z_clip_bps{200.0}; // 50~300 bps 视币种调
  double band_leak{0.02};   // ★band 内漏一点：0.02~0.1 推荐

  double fK{0.00005};
  double fL{0.00005};
  double c{0.5};
  double buffer{0.0};
};

struct Result {
  std::array<double, 2> fp{{-1.0, -1.0}}; // [bid, ask]
  bool ok{false};

  // diagnostics（可选）
  double g_m{0.0};
  double m_fair{0.0};
  double s_fair{0.0};
  std::array<double, 6> w{{0, 0, 0, 0, 0, 0}};
  std::array<double, 6> g{{0, 0, 0, 0, 0, 0}};
  std::array<double, 6> s{{0, 0, 0, 0, 0, 0}};
};

// 6通道顺序固定：
// 0=bn_depth, 1=bn_bbo, 2=ok_depth, 3=ok_bbo, 4=cb_depth, 5=cb_bbo
Result compute_fp_bidask_3ex2src(const Quote2 &aim,
                                 const std::array<Ex2, 3> &leads,
                                 uint64_t now_ts, const Params &p,
                                 data::DigitalFpState &state,
                                 bool verbose = false);

} // namespace digital_fp
