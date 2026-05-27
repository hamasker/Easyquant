#pragma once

#include "nova_api_data_type.h"
#include "nova_api_quote_struct.h"
#include "nova_api_struct.h"

#include "nova_base.h"

#include "base/base_trie.h"
#include "base/csv.h"

#include "quote/quote_util.h"

USE_NOVA_NAMESPACE(base)
BEGIN_NOVA_NAMESPACE(quote)

inline bool double_gt(double l, double r) {
  return DoubleGT(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_ge(double l, double r) {
  return DoubleGE(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_lt(double l, double r) {
  return DoubleLT(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_le(double l, double r) {
  return DoubleLE(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_eq(double l, double r) {
  return DoubleEQ(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_ne(double l, double r) {
  return DoubleNE(l, r, COIN_DOUBLE_EPSILON);
}
inline bool double_zero(double v) { return double_eq(v, 0); }

inline int64_t double_round2multiple(double v, double m) {
  return std::round(v / m);
}
inline int64_t double_ceil2multiple(double v, double m, double e = 0) {
  return std::ceil(v / m - e);
}
inline int64_t double_floor2multiple(double v, double m, double e = 0) {
  return std::floor(v / m + e);
}

inline int64_t double_round(double v, double m) {
  return std::round(v / m) * m;
}
inline int64_t double_ceil(double v, double m, double e = 0) {
  return std::ceil(v / m - e) * m;
}
inline int64_t double_floor(double v, double m, double e = 0) {
  return std::floor(v / m + e) * m;
}

inline bool SplitInstrumentString(const char *src, const char *&instrument,
                                  const char *&market) {
  auto *sp = strchr(src, '.');
  if (sp == nullptr) {
    return false;
  }
  instrument = src;
  market = sp + 1;
  return true;
}

inline bool
SplitInstrumentString(std::pair<std::string, std::string> &out_ins_mkr,
                      const char *src) {
  auto *sp = strchr(src, '.');
  if (sp == nullptr) {
    return false;
  }
  out_ins_mkr.first = std::string(src, sp - src);
  out_ins_mkr.second = sp + 1;
  return true;
}

inline bool LoadCoinConfigInstruments(const Config *cfg,
                                      const char *config_path,
                                      std::vector<InstrumentId> &out) {
  if (cfg == nullptr || config_path == nullptr) {
    return false;
  }
  std::vector<picojson::value> tmp_vec;
  std::vector<InstrumentId> tmp_out;

  if (!cfg->GetItemValue(config_path, tmp_vec)) {
    return false;
  }
  for (auto &i : tmp_vec) {
    if (!i.is<std::string>()) {
      return false;
    }

    auto raw = i.get<std::string>();

    InstrumentId inst = InstrumentId::Create(raw);
    if (!inst.Valid())
      return false;
    tmp_out.push_back(inst);
  }
  out = std::move(tmp_out);
  return true;
}

inline bool IsOrderWorking(const ::nova::trade::NovaOrder *o) {
  return o != nullptr && DoubleGT(o->qty_left, 0);
}

class InstrumentBaseInfoManager {
private:
  using MapT = Trie<InstrumentBaseInfo, CoinCharVal>;

public:
  InstrumentBaseInfoManager() { default_mgr_ = MapT::Create(); }
  InstrumentBaseInfoManager(InstrumentBaseInfoManager &&rhs) {
    default_mgr_ = rhs.default_mgr_;
    dir_ = std::move(rhs.dir_);
    last_trading_day_ = rhs.last_trading_day_;
    rhs.default_mgr_ = nullptr;
  }
  int size() { return default_mgr_->Count(); }

private:
  InstrumentBaseInfoManager(const InstrumentBaseInfoManager &rhs) {
    default_mgr_ = MapT::Create();
    dir_ = rhs.dir_;
    last_trading_day_ = rhs.last_trading_day_;
    rhs.default_mgr_->Walk(
        [&](const std::string &key, const InstrumentBaseInfo &data) {
          default_mgr_->Insert(key.c_str(), data);
        });
  }

public:
  static InstrumentBaseInfoManager Copy(const InstrumentBaseInfoManager &rhs) {
    return InstrumentBaseInfoManager(rhs);
  }
  template <typename Func> void Walk(Func func) {
    default_mgr_->Walk(std::forward<Func>(func));
  }

  void AllData(std::vector<std::pair<std::string, InstrumentBaseInfo>> &res) {
    default_mgr_->AllData(res);
  }

public:
  ~InstrumentBaseInfoManager() { delete default_mgr_; }

public:
  const InstrumentBaseInfo *get(const InstrumentId::Key &k) const {
    auto iter = default_mgr_->Find(k.c_str());
    if (iter == nullptr) {
      return nullptr;
    }
    return iter;
  }

  bool set(const InstrumentId::Key &k, const InstrumentBaseInfo *base_info) {
    default_mgr_->Insert(k.c_str(), *base_info);
    return true;
  }

private:
  MapT *default_mgr_;
  std::string dir_;

public:
  int32_t last_trading_day_ = -1;

public:
  static constexpr auto BASE_INFO_FILE_COLUMN_SIZE = 11;

  static constexpr auto CONFIG_BASE_INFO_PATH = "BaseInfo.base_info_path";
  static constexpr auto CONFIG_BASE_INFO_DIR = "BaseInfo.base_info_dir";

  bool LoadBaseInfo(int32_t /*day*/) {
    // day = 1;
    return false;
  } // todo
  bool LoadBaseInfo(const Config /*cfg*/) {
    // cfg = nullptr;
    return false;
  } // todo
  bool LoadBaseInfo(const std::string &path, const std::string &dir = "") {
    INFO_FLOG("LoadBaseInfo: {} {}", path, dir);
    return false;
  } // todo

private:
  static bool _LoadBaseInfo(MapT /*mgr*/, const char /*base_info_file_path*/) {
    // mgr = nullptr;
    // base_info_file_path = nullptr;
    return false;
  } // todo
};
END_NOVA_NAMESPACE(quote)