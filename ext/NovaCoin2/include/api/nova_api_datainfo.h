#pragma once

#include "nova_api_quote_struct.h"
#include <bitset>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) ||               \
    defined(_M_IX86)
#include <immintrin.h>
#endif

BEGIN_NOVA_NAMESPACE(quote)

enum StreamBufferTag : int8_t {
  BUFFER_TAG_NORMAL = 0,
};

struct alignas(32) JSDataMask {
  static constexpr auto CAPACITY = 256;

  void reset() { mask_.reset(); }
  void set(size_t idx) { mask_.set(idx); }
  bool test(size_t idx) const { return mask_.test(idx); }

private:
  std::bitset<CAPACITY> mask_;
};

template <class _Impl, class _DataT = void> class StreamBuffer {
public:
  using DataT = _DataT;
  using Impl = _Impl;

  StreamBuffer(int32_t max_lines, int8_t type, StreamBufferTag tag)
      : end_idx_(0), max_lines_(max_lines), type_(type), tag_(tag) {}

  DataT *PreparePush() { return dst(); }

  void EndPush() { ++end_idx_; }
  void Clear() { end_idx_ = 0; }

public:
  DataT *dst() { return (*this)[end_idx_]; }

  int64_t capacity() const { return max_lines_; }

  int64_t beg_li() const { return std::max(int64_t(0), end_idx_ - max_lines_); }

  int64_t end_li() const { return end_idx_; }

  int64_t size() const {
    auto ret = end_li() - beg_li();
    NOVA_ASSERT(ret >= 0);
    return ret;
  }

  int64_t empty() const { return size() == 0; }

  void set_type(int8_t t) { type_ = t; }
  int8_t type() const { return type_; }

  void set_tag(StreamBufferTag t) { tag_ = t; }
  int64_t tag() const { return tag_; }

public:
  DataT *front() { return (*this)[beg_li()]; }
  const DataT *front() const { return (*this)[beg_li()]; }
  DataT *back() {
    NOVA_ASSERT(!empty());
    return (*this)[end_li() - 1];
  }
  const DataT *back() const {
    NOVA_ASSERT(!empty());
    return (*this)[end_li() - 1];
  }
  const DataT *back(int64_t n) const {
    if (size() <= n) {
      return nullptr;
    }
    return (*this)[end_li() - 1 - n];
  }

  DataT *operator[](int64_t li) {
    NOVA_ASSERT(li >= 0 && li >= beg_li() && li <= end_li());
    return const_cast<DataT *>(static_cast<const Impl *>(this)->do_get(li));
  }

  const DataT *operator[](int64_t li) const {
    NOVA_ASSERT(li >= 0 && li >= beg_li() && li <= end_li());
    return static_cast<const Impl *>(this)->do_get(li);
  }

private:
  int64_t end_idx_ = 0;

  int32_t max_lines_;
  int8_t type_;
  StreamBufferTag tag_;
};

class VariantStreamBuffer : public StreamBuffer<VariantStreamBuffer> {
public:
  VariantStreamBuffer(int32_t max_lines, int32_t col_bytes, int8_t type,
                      StreamBufferTag tag)
      : StreamBuffer(max_lines, type, tag), col_bytes_(col_bytes),
        buf_(new char[max_lines * col_bytes]{}) {}

  int32_t col_bytes() const { return col_bytes_; }
  const void *do_get(int64_t li) const {
    return buf_.get() + col_bytes_ * (li % capacity());
  }

  void Push(const void *data) {
    memcpy(PreparePush(), data, col_bytes_);
    EndPush();
  }

private:
  int32_t col_bytes_;
  std::unique_ptr<char[]> buf_;
};

template <class T>
class ScalarStreamBuffer : public StreamBuffer<ScalarStreamBuffer<T>, T> {
public:
  ScalarStreamBuffer(int32_t max_lines, int8_t type, StreamBufferTag tag)
      : StreamBuffer<ScalarStreamBuffer<T>, T>(max_lines, type, tag),
        buf_(new T[max_lines]) {
    static_assert(std::is_scalar<T>::value, "");
  }
  const T *do_get(int64_t li) const {
    return buf_.get() + li % this->capacity();
  }

  void Push(T v) {
    *this->PreparePush() = v;
    this->EndPush();
  }

private:
  std::unique_ptr<T[]> buf_;
};

using fvalue_t = double;
using fbuf_t = ScalarStreamBuffer<fvalue_t>;
using qbuf_t = VariantStreamBuffer;

template <class BufT>
  requires std::is_base_of_v<
      StreamBuffer<typename BufT::Impl, typename BufT::DataT>, BufT>
struct BufferViewer {
  int64_t last_end_li = 0;
  const BufT *buf = nullptr;

  int64_t beg_li() const { return std::max(last_end_li, buf->beg_li()); }
  int64_t end_li() const { return buf->end_li(); }

  int64_t size() const {
    auto ret = end_li() - beg_li();
    NOVA_ASSERT(ret >= 0);
    return ret;
  }

  int64_t empty() const { return size() == 0; }
  bool buf_empty() const { return buf->empty(); }

  const typename BufT::DataT *back() const { return buf->back(); }

  BufferViewer(const BufT *b) : last_end_li(b->end_li()), buf(b) {}
};

using fbuf_view = BufferViewer<fbuf_t>;
using qbuf_view = BufferViewer<qbuf_t>;

class DataInfo {
public:
  static constexpr double EMA_VALID_RATE = 1e-4;
  static constexpr double EMA_DELAY = 0.05;

  struct Param {
    int64_t overtime = -1;

    int32_t buf_capacity = 512;
    int32_t buf_col_bytes = 0;
    NOVA_COIN_QUOTE_TYPE buf_type = NOVA_COIN_QUOTE_INIT;
    StreamBufferTag buf_tag = StreamBufferTag::BUFFER_TAG_NORMAL;
  };

  DataInfo(const Param &p)
      : param_(p),
        buffer_(p.buf_capacity, p.buf_col_bytes, p.buf_type, p.buf_tag) {}

  bool Push(const void *data, int64_t localtime) {
    buffer_.Push(data);
    if (LIKELY(last_time_ != 0)) {
      delta_ = localtime - last_time_;

      if (param_.overtime >= 0) {
        smoothed_delay_ =
            smoothed_delay_ * (1 - EMA_DELAY) + delta_ * EMA_DELAY;
        if (smoothed_delay_ < param_.overtime) {
          status_ = true;
          smoothed_valid_rate_ =
              smoothed_valid_rate_ * (1 - EMA_VALID_RATE) + EMA_VALID_RATE;
        } else {
          status_ = false;
          smoothed_valid_rate_ *= (1 - EMA_VALID_RATE);
        }
      }
    }
    last_time_ = localtime;
    return true;
  }

public:
  const VariantStreamBuffer &buffer() const { return buffer_; }

  qbuf_view create_qbuf_view() const { return {&buffer_}; }

  bool is_valid() const { return status_; }
  double valid_rate() const { return smoothed_valid_rate_; }

  NOVA_COIN_QUOTE_TYPE quote_type() const { return param_.buf_type; }

private:
  Param param_;
  int64_t last_time_ = 0;
  int64_t delta_ = 0;

  qbuf_t buffer_;

  bool status_ = true;
  double smoothed_delay_ = 0.0;
  double smoothed_valid_rate_ = 1;
};

class DataInfoManager {
public:
  struct DIParam {
    DataInfo::Param dip{};
    bool trigger = false;
  };
  enum TriggerType {
    TRIGGER_TYPE_MASK,
  };
  struct Param {
    TriggerType trigger_type = TRIGGER_TYPE_MASK;
    std::vector<DIParam> datainfo;
  };

  bool Init(const Param &param) {
    trigger_type_ = param.trigger_type;
    if (param.datainfo.size() > JSDataMask::CAPACITY) {
      return false;
    }
    tigger_mask_.reset();
    datainfo_.clear();
    datainfo_.reserve(param.datainfo.size());
    for (size_t i = 0; i < param.datainfo.size(); ++i) {
      datainfo_.emplace_back(param.datainfo[i].dip);
      if (param.datainfo[i].trigger) {
        tigger_mask_.set(i);
      }
    }
    return true;
  }

  bool Push(size_t ii, const void *data, int64_t local_ns) {
    if (ii >= datainfo_.size()) {
      return false;
    }
    auto bret = datainfo_[ii].Push(data, local_ns);
    if (!bret) {
      return false;
    }
    return true;
  }

  std::vector<qbuf_view> create_qbuf_views() const {
    std::vector<qbuf_view> ret;
    for (auto &di : datainfo_) {
      ret.push_back(di.create_qbuf_view());
    }
    return ret;
  }

  const std::vector<DataInfo> &datainfo() const { return datainfo_; }

private:
  TriggerType trigger_type_;
  JSDataMask tigger_mask_;
  std::vector<DataInfo> datainfo_;
};

END_NOVA_NAMESPACE(quote)