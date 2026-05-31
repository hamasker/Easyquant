#include "digital_fp_calculator.h"

namespace digital_fp {

static inline bool wp_ok(const std::array<double, 2> &wp) {
  return (wp[0] > 0.0) && (wp[1] > 0.0) && (wp[0] <= wp[1]);
}

static inline double clampd(double x, double lo, double hi) {
  return (x < lo) ? lo : (x > hi) ? hi : x;
}

static inline double log_mid(const std::array<double, 2> &wp) {
  return 0.5 * (std::log(wp[0]) + std::log(wp[1]));
}

static inline double log_spread(const std::array<double, 2> &wp) {
  return std::log(wp[1]) - std::log(wp[0]);
}

static inline double age_weight(uint64_t now, uint64_t last,
                                double inv_tau_ns) {
  const double age = double(now - last);
  return (age <= 0.0) ? 1.0 : std::exp(-age * inv_tau_ns);
}

static inline void sort_prefix(double *a, int n) { std::sort(a, a + n); }

static inline double median_n(double *a, int n) {
  sort_prefix(a, n);
  const int mid = n >> 1;
  if (n & 1)
    return a[mid];
  return 0.5 * (a[mid - 1] + a[mid]);
}

Result compute_fp_bidask_3ex2src(const Quote2 &aim,
                                 const std::array<Ex2, 3> &leads,
                                 uint64_t now_ts, const Params &p,
                                 data::DigitalFpState &state, bool verbose) {
  Result r;

  static constexpr const char *EX_NAME[3] = {"bn", "ok", "cb"};
  static constexpr const char *SRC_NAME[2] = {"depth", "bbo"};

  auto ns_to_ms = [](uint64_t ns) -> double { return double(ns) * 1e-6; };

  const double inv_tau_ns = 1.0 / (p.tau_age_ms * 1e6);
  const double s_min_log = p.s_min_bps * 1e-4;
  const double s_max_log = p.s_max_bps * 1e-4;
  const double g_cap_log = p.g_cap_bps * 1e-4;

  const double var_init = (p.var_init_bps * 1e-4) * (p.var_init_bps * 1e-4);

  // anti-windup params (log尺度)
  const double g_gate_log =
      p.g_gate_bps * 1e-4; // gate 打开宽度 (超出 band 后的过度宽度)
  const double g_reset_log = p.g_reset_bps * 1e-4; // band 附近 reset 宽度
  const double z_clip_log = p.z_clip_bps * 1e-4;

  // ===== 保留你原来的 params 打印（不删）=====
  if (verbose) {
    DEBUG_FLOG("[FP] params tau_age_ms={} inv_tau_ns={} kappa_m={} beta_s={} "
               "s_min_bps={} s_max_bps={} g_cap_bps={} k_mad={} eps={} "
               "ex_w=[{},{},{}] src_u=[{},{}]",
               p.tau_age_ms, inv_tau_ns, p.kappa_m, p.beta_s, p.s_min_bps,
               p.s_max_bps, p.g_cap_bps, p.k_mad, p.eps, p.ex_w[0], p.ex_w[1],
               p.ex_w[2], p.src_u[0], p.src_u[1]);
    // 额外：打印 rho/gamma（新增，不影响你原打印）
    DEBUG_FLOG("[FP] ret params rho_z={} gamma_z={} alpha_m={} alpha_var={} "
               "alpha_base={} var_init_bps={}",
               p.rho_z, p.gamma_z, p.alpha_m, p.alpha_var, p.alpha_base,
               p.var_init_bps);
    // 额外：打印 anti-windup 参数（新增）
    DEBUG_FLOG("[FP] anti params g_gate_bps={} g_reset_bps={} z_clip_bps={}",
               p.g_gate_bps, p.g_reset_bps, p.z_clip_bps);
    // 额外：打印 leak（新增，不影响你原打印）
    DEBUG_FLOG("[FP] leak param band_leak={}", p.band_leak);
  }

  if (!aim.ok || !wp_ok(aim.px)) {
    if (verbose)
      DEBUG_FLOG(
          "[FP] aim invalid ok={} wp_bid={} wp_ask={} local_ts={} age_ms={}",
          aim.ok, aim.px[0], aim.px[1], aim.local_ts,
          aim.local_ts ? ns_to_ms(now_ts - aim.local_ts) : -1.0);
    return r;
  }

  const double m_k = log_mid(aim.px);
  const double s_k_raw = log_spread(aim.px);
  const double s_k = clampd(s_k_raw, s_min_log, s_max_log);

  // ===== 新增：计算 r_aim（用于 anti-windup）=====
  double r_aim = 0.0;
  if (!state.has_prev_m_k) {
    state.has_prev_m_k = true;
    state.prev_m_k = m_k;
    r_aim = 0.0;
  } else {
    r_aim = m_k - state.prev_m_k;
    state.prev_m_k = m_k;
  }
  if (verbose)
    DEBUG_FLOG("[FP] aim ret r_aim={} prev_m_k={}", r_aim, state.prev_m_k);

  // ===== 保留你原来的 aim 打印（不删）=====
  if (verbose)
    DEBUG_FLOG("[FP] aim wp=[{},{}] local_ts={} age_ms={} m_k={} s_raw={} "
               "s_clamp={} range_s=[{},{}]",
               aim.px[0], aim.px[1], aim.local_ts,
               ns_to_ms(now_ts - aim.local_ts), m_k, s_k_raw, s_k, s_min_log,
               s_max_log);

  r.g_m = 0.0;

  std::array<double, 6> g{};      // mid gap (clamped)
  std::array<double, 6> s{};      // spread (clamped)
  std::array<double, 6> raw{};    // raw weight (src_u * w_age * w_qual)
  std::array<double, 6> w{};      // normalized weight
  std::array<double, 6> w_age{};  // age weight
  std::array<double, 6> w_qual{}; // quality weight
  // return-only: 每个通道 return (log-mid diff)
  std::array<double, 6> r_ret{}; // r_j = m_j - prev_m_j

  // ===== 通道循环: 算 g/s/raw, 更新噪声状态+prev_m =====
  for (int ex = 0; ex < 3; ++ex) {
    for (int src = 0; src < 2; ++src) {
      const Quote2 &q = (src == 0) ? leads[ex].depth : leads[ex].bbo;
      const int j = ex * 2 + src;

      if (!q.ok || !wp_ok(q.px)) {
        raw[j] = 0.0;
        r_ret[j] = 0.0;
        if (verbose)
          DEBUG_FLOG("[FP] ch {}_{} j={} INVALID ok={} wp=[{},{}] local_ts={} "
                     "age_ms={}",
                     EX_NAME[ex], SRC_NAME[src], j, q.ok, q.px[0], q.px[1],
                     q.local_ts,
                     q.local_ts ? ns_to_ms(now_ts - q.local_ts) : -1.0);
        continue;
      }

      const double m = log_mid(q.px);

      const double g_raw = m - m_k;
      g[j] = clampd(g_raw, -g_cap_log, g_cap_log);

      const double s_raw = log_spread(q.px);
      s[j] = clampd(s_raw, s_min_log, s_max_log);

      w_age[j] = age_weight(now_ts, q.local_ts, inv_tau_ns);

      // ====== return-only：更新 prev_m 得到 r_ret ======
      if (verbose)
        DEBUG_FLOG(
            "[FP] ch {}_{} j={} m={} prev_m={} r_ret=[{},{},{},{},{},{}]",
            EX_NAME[ex], SRC_NAME[src], j, m, state.ch[j].prev_m, r_ret[0],
            r_ret[1], r_ret[2], r_ret[3], r_ret[4], r_ret[5]);

      auto &st = state.ch[j];
      if (!st.has_prev_m) {
        st.has_prev_m = true;
        st.prev_m = m;
        r_ret[j] = 0.0;
      } else {
        r_ret[j] = m - st.prev_m;
        st.prev_m = m;
      }

      // ====== w_qual：var/var_base ratio ======
      double err = 0.0;
      if (!st.inited) {
        st.inited = true;
        st.ema_m = m;
        st.var = var_init;
        st.var_base = var_init;
        err = 0.0;
      } else {
        err = m - st.ema_m;
        st.ema_m += p.alpha_m * err;

        const double e2 = err * err;
        st.var = (1.0 - p.alpha_var) * st.var + p.alpha_var * e2;
        if (st.var < p.var_floor)
          st.var = p.var_floor;

        st.var_base =
            (1.0 - p.alpha_base) * st.var_base + p.alpha_base * st.var;
        if (st.var_base < p.var_floor)
          st.var_base = p.var_floor;
      }

      const double ratio = st.var / (st.var_base + p.eps);
      w_qual[j] = 1.0 / (1.0 + ratio);

      raw[j] = p.src_u[src] * w_age[j] * w_qual[j];

      // ======= 保留你原来的打印（不删） =======
      if (verbose) {
        DEBUG_FLOG("[FP] ch {}_{} j={} wp=[{},{}] local_ts={} age_ms={} m={} "
                   "g_raw={} g_clamp={} cap_g={} s_raw={} s_clamp={} w_age={} "
                   "w_qual={} src_u={} raw={}",
                   EX_NAME[ex], SRC_NAME[src], j, q.px[0], q.px[1], q.local_ts,
                   ns_to_ms(now_ts - q.local_ts), m, g_raw, g[j], g_cap_log,
                   s_raw, s[j], w_age[j], w_qual[j], p.src_u[src], raw[j]);
        DEBUG_FLOG(
            "[FP] ch {}_{} j={} noise ema_m={} err={} var={} var_base={} "
            "ratio={} w_qual={}",
            EX_NAME[ex], SRC_NAME[src], j, st.ema_m, err, st.var, st.var_base,
            ratio, w_qual[j]);
        DEBUG_FLOG("[FP] ch {}_{} j={} ret r={} prev_m={}", EX_NAME[ex],
                   SRC_NAME[src], j, r_ret[j], st.prev_m);
      }
    }
  }

  // ===== 鲁棒剔除 =====
  {
    double buf[6];
    int n = 0;
    for (int i = 0; i < 6; ++i) {
      if (raw[i] > 0.0)
        buf[n++] = g[i];
    }

    if (verbose)
      DEBUG_FLOG("[FP] robust pre n_valid={} raw=[{},{},{},{},{},{}] "
                 "g=[{},{},{},{},{},{}]",
                 n, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], g[0], g[1],
                 g[2], g[3], g[4], g[5]);

    if (n >= 3) {
      double tmp_med[6];
      for (int i = 0; i < n; ++i)
        tmp_med[i] = buf[i];
      const double med = median_n(tmp_med, n);

      double tmp_mad[6];
      for (int i = 0; i < n; ++i)
        tmp_mad[i] = std::abs(buf[i] - med);
      const double mad = median_n(tmp_mad, n);

      const double thr = p.k_mad * (mad + p.eps);

      if (verbose)
        DEBUG_FLOG("[FP] robust stat med={} mad={} thr={} (k_mad={})", med, mad,
                   thr, p.k_mad);

      for (int i = 0; i < 6; ++i) {
        if (raw[i] > 0.0 && std::abs(g[i] - med) > thr) {
          if (verbose)
            DEBUG_FLOG("[FP] robust DROP i={} g={} |g-med|={} raw={} -> 0", i,
                       g[i], std::abs(g[i] - med), raw[i]);
          raw[i] = 0.0;
          r_ret[i] = 0.0;
        }
      }
    }
  }

  if (verbose)
    DEBUG_FLOG("[FP] robust post raw=[{},{},{},{},{},{}]", raw[0], raw[1],
               raw[2], raw[3], raw[4], raw[5]);

  // ===== 分层归一 =====
  double wsum = 0.0;
  for (int ex = 0; ex < 3; ++ex) {
    const int j0 = ex * 2;
    const int j1 = ex * 2 + 1;
    const double sum_ex = raw[j0] + raw[j1];

    if (verbose)
      DEBUG_FLOG("[FP] ex {} sum_ex={} raw_depth={} raw_bbo={} ex_w={}",
                 EX_NAME[ex], sum_ex, raw[j0], raw[j1], p.ex_w[ex]);

    if (sum_ex <= 0.0) {
      w[j0] = 0.0;
      w[j1] = 0.0;
      continue;
    }

    const double Wex = p.ex_w[ex];
    w[j0] = Wex * (raw[j0] / sum_ex);
    w[j1] = Wex * (raw[j1] / sum_ex);
    wsum += (w[j0] + w[j1]);

    if (verbose)
      DEBUG_FLOG("[FP] ex {} w_pre depth={} bbo={} wsum_now={}", EX_NAME[ex],
                 w[j0], w[j1], wsum);
  }

  if (wsum <= 0.0) {
    if (verbose)
      DEBUG_FLOG("[FP] wsum<=0 fallback aim wp.");
    r.fp = aim.px;
    r.ok = true;
    return r;
  }

  for (int i = 0; i < 6; ++i)
    w[i] /= wsum;

  if (verbose)
    DEBUG_FLOG("[FP] w_norm=[{},{},{},{},{},{}] (wsum_before_norm={})", w[0],
               w[1], w[2], w[3], w[4], w[5], wsum);

  // ===== 1) 融合 gap =====
  double g_m = 0.0;
  for (int i = 0; i < 6; ++i)
    g_m += w[i] * g[i];

  // ===== no-arb band（固定参数）=====
  // band 宽度：b = fK + fL + c * s_k + buffer
  const double b = p.fK + p.fL + p.c * s_k + p.buffer;

  // ===== 2) 融合 return（lead），更新 z: rho_z*z+gate_x*(r_lead-r_aim) =====
  double r_lead = 0.0;
  for (int i = 0; i < 6; ++i)
    r_lead += w[i] * r_ret[i];

  if (!state.z_inited) {
    state.z_inited = true;
    state.z = 0.0;
  }

  const double ad = std::abs(g_m);
  const double x = ad - b; // 超出 band 的部分

  // gate：饱和型(让 g_gate_bps 有意义)
  double gate_x = 0.0;
  if (x > 0.0) {
    gate_x = x / (x + g_gate_log + p.eps);
  }

  auto to_bps = [](double x) { return x * 1e4; }; // log -> bps
  if (verbose)
    DEBUG_FLOG(
        "[FP] gate_dbg |g_m|={}bps b={}bps x={}bps g_gate={}bps gate_x={}",
        to_bps(std::abs(g_m)), to_bps(b), to_bps(x), p.g_gate_bps, gate_x);

  // z 更新门控
  const double gate_acc = (x > 0.0 ? 1.0 : 0.0); // 只要在 band 外就积累
  state.z = p.rho_z * state.z + gate_acc * (r_lead - r_aim);

  // z clip
  if (state.z > z_clip_log)
    state.z = z_clip_log;
  if (state.z < -z_clip_log)
    state.z = -z_clip_log;

  // reset：深入 band 内才清零
  // x < -g_reset => 明确回到 band 内
  if (x < -g_reset_log) {
    state.z = 0.0;
  }

  // ===== 3) dm：deadband gap + band leak + gated ret =====
  // hard deadband part
  double d_eff = 0.0;
  if (x > 0.0)
    d_eff = (g_m > 0.0 ? 1.0 : -1.0) * x;

  // ★ band 内漏一点（即使 x<=0 也会推一点）
  d_eff += p.band_leak * g_m;

  const double dm_gap = p.kappa_m * d_eff;

  const double dm_ret_raw = p.gamma_z * state.z;
  const double dm_ret = dm_ret_raw * gate_x; // 可改 gate_x*gate_x 更柔和

  const double dm = dm_gap + dm_ret;
  const double m_fair = m_k + dm;

  if (verbose) {
    DEBUG_FLOG("[FP] band fK={} fL={} c={} sK={} b={} g_m={} |g_m|={} x={} "
               "g_gate={} gate_x={} d_eff={} leak={} dm_gap={}",
               p.fK, p.fL, p.c, s_k, b, g_m, ad, x, p.g_gate_bps, gate_x, d_eff,
               p.band_leak, dm_gap);
  }

  // ===== 保留你原来的 mid_fuse 打印（不删）=====
  if (verbose) {
    DEBUG_FLOG("r_lead: {}, z: {}, rho_z: {}, r_ret: [{},{},{},{},{},{}]",
               r_lead, state.z, p.rho_z, r_ret[0], r_ret[1], r_ret[2], r_ret[3],
               r_ret[4], r_ret[5]);
    DEBUG_FLOG("kappa_m: {}, g_m: {}, gamma_z: {}, state.z: {}", p.kappa_m, g_m,
               p.gamma_z, state.z);
    DEBUG_FLOG(
        "[FP] mid_fuse g=[{},{},{},{},{},{}] w=[{},{},{},{},{},{}] g_m={} "
        "dm={} m_fair={}",
        g[0], g[1], g[2], g[3], g[4], g[5], w[0], w[1], w[2], w[3], w[4], w[5],
        g_m, dm, m_fair);

    DEBUG_FLOG(
        "[FP] ret_fuse r=[{},{},{},{},{},{}] r_lead={} r_aim={} z={} rho_z={} "
        "gate_x={} dm_gap={} dm_ret_raw={} dm_ret={} gamma_z={}",
        r_ret[0], r_ret[1], r_ret[2], r_ret[3], r_ret[4], r_ret[5], r_lead,
        r_aim, state.z, p.rho_z, gate_x, dm_gap, dm_ret_raw, dm_ret, p.gamma_z);
  }

  // spread
  double s_fair = s_k;
  if (p.beta_s > 0.0) {
    double s_lead = 0.0;
    for (int i = 0; i < 6; ++i)
      s_lead += w[i] * s[i];
    const double s_lead_clamp = clampd(s_lead, s_min_log, s_max_log);
    s_fair = clampd((1.0 - p.beta_s) * s_k + p.beta_s * s_lead_clamp, s_min_log,
                    s_max_log);

    if (verbose)
      DEBUG_FLOG(
          "[FP] spread_fuse beta_s={} s_k={} s_lead_raw={} s_lead_clamp={} "
          "s_fair={}",
          p.beta_s, s_k, s_lead, s_lead_clamp, s_fair);
  } else {
    if (verbose)
      DEBUG_FLOG("[FP] spread_fuse beta_s=0 use s_k={} -> s_fair={}", s_k,
                 s_fair);
  }

  const double half_s = 0.5 * s_fair;
  r.fp[0] = std::exp(m_fair - half_s);
  r.fp[1] = std::exp(m_fair + half_s);
  r.ok = true;

  // diagnostics
  r.g_m = g_m;
  r.m_fair = m_fair;
  r.s_fair = s_fair;
  r.w = w;
  r.g = g;
  r.s = s;

  if (verbose)
    DEBUG_FLOG("[FP] out fp_bid={} fp_ask={} aim_wp=[{},{}]", r.fp[0], r.fp[1],
               aim.px[0], aim.px[1]);

  // ====== ONLINE EVAL (place near the end, before return) ======
  {
    // 是否打印在线评估（可以独立于 verbose）
    const bool eval_verbose = false; // 你可以改成 CFG_ 开关或传参

    auto to_bps = [](double x) -> double { return x * 1e4; }; // log -> bps

    // EWMA 的更新速度：0.02 表示约 1/0.02=50 个样本（0.5s）时间常数
    constexpr double alpha_eval = 0.02;

    auto ewma = [&](double &ema, double v) {
      ema = (1.0 - alpha_eval) * ema + alpha_eval * v;
    };

    auto &E = state.eval;
    E.sample_cnt++;

    const double dev_bps = to_bps(std::abs(m_fair - m_k)); // 偏离
    double step_bps = 0.0;
    if (!E.inited) {
      E.inited = true;
      E.prev_m_fair = m_fair;
    } else {
      step_bps = to_bps(std::abs(m_fair - E.prev_m_fair));
      E.prev_m_fair = m_fair;
    }

    const double gate = gate_x;
    const double xpos = (x > 0.0) ? 1.0 : 0.0;

    // overshoot proxy：
    // 直觉：已经回到 band 内（x<=0），但 ret 推动项仍不小/仍在推 => 有过冲风险
    // 你这里用 dm_ret_raw 的大小作为 proxy（如果你变量名不同，替换一下）
    const double dm_ret_raw_bps = to_bps(std::abs(dm_ret_raw));
    const double overshoot =
        (x <= 0.0 && dm_ret_raw_bps > 1.0) ? 1.0 : 0.0; // 1bps门槛可调

    const double abs_z_bps = to_bps(std::abs(state.z));

    ewma(E.ema_abs_dev_bps, dev_bps);
    ewma(E.ema_abs_step_bps, step_bps);
    ewma(E.ema_gate, gate);
    ewma(E.ema_xpos, xpos);
    ewma(E.ema_overshoot, overshoot);
    ewma(E.ema_abs_z_bps, abs_z_bps);

    // 每 1 秒打印一次
    const uint64_t NS_1S = 1'000'000'000ULL;
    if (eval_verbose &&
        (E.last_report_ts == 0 || now_ts - E.last_report_ts >= NS_1S)) {
      E.last_report_ts = now_ts;

      DEBUG_FLOG("[FP][EVAL] cnt={} dev_bps_ema={} step_bps_ema={} gate_ema={} "
                 "xpos_ema={} overshoot_ema={} abs_z_bps_ema={}",
                 E.sample_cnt, E.ema_abs_dev_bps, E.ema_abs_step_bps,
                 E.ema_gate, E.ema_xpos, E.ema_overshoot, E.ema_abs_z_bps);

      // 简单建议（不自动改参数，只给提示）
      if (E.ema_abs_dev_bps < 0.2 && E.ema_xpos < 0.01) {
        DEBUG_FLOG("[FP][SUGGEST] fp too close to aim. Try band_leak↑ (e.g. "
                   "0.02->0.05) or reduce band (fK/fL) or use soft gate.");
      }
      if (E.ema_abs_step_bps > 2.0) {
        DEBUG_FLOG("[FP][SUGGEST] fp too jumpy. Try band_leak↓, gamma_z↓, or "
                   "g_gate_bps↑ (slower gate).");
      }
      if (E.ema_overshoot > 0.05) {
        DEBUG_FLOG(
            "[FP][SUGGEST] overshoot risk. Try gamma_z↓, z_clip_bps↓, or "
            "g_reset_bps↑.");
      }
      if (E.ema_abs_z_bps > 100.0) {
        DEBUG_FLOG("[FP][SUGGEST] z too large. Consider z_clip_bps↓ or rho_z↓ "
                   "or stronger gate/reset.");
      }
    }
  }
  // ====== ONLINE EVAL END ======
  return r;
}

} // namespace digital_fp
