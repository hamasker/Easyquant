#pragma once

#include <array>
#include <stdexcept>
#include <vector>
// 最大buffer长度 element长度
template <size_t MaxLength, size_t ElementLength> class CircularBuffer {
public:
  // 默认构造函数，使用默认最大长度
  CircularBuffer() : index_(0), is_full_(false) {}

  // 添加新值
  void add(const std::array<double, ElementLength> &value);
  void add(double value); // 新增重载, 用于直接添加double值

  // 返回缓冲区中的所有值
  std::array<std::array<double, ElementLength>, MaxLength> get_all() const;

  // Eigen::MatrixXd get_all_eigen() const;

  // 获取最新的一个值
  std::array<double, ElementLength> get_latest() const;

  double get_last() const;

  // 判断缓冲区是否为空
  bool is_empty() const;

  // 新增size方法声明
  size_t size() const;

  std::array<double, MaxLength> get_column(size_t column_index) const;

  std::vector<std::array<double, ElementLength>> get_latest_N(size_t N) const;

  std::vector<double> get_latest_N_vec(size_t N) const;

  std::array<std::array<double, ElementLength>, 3000> get_latest_3000() const;

  std::array<std::array<double, ElementLength>, 1000> get_latest_1000() const;

  constexpr size_t get_max_length() const { return MaxLength; };

  constexpr size_t get_element_length() const { return ElementLength; };

  template <size_t COL> struct column_accessor {
    static_assert(COL < ElementLength, "Invalid column");
    explicit column_accessor(const CircularBuffer &buffer, size_t N) {
      size_t current_size = buffer.is_full_ ? MaxLength : buffer.index_;
      current_size -= N;
      if (buffer.is_full_)
        idx = (buffer.index_ + current_size) % MaxLength;
      else
        idx = current_size;
      data = (double *)&buffer.buffer_;
    };
    void operator++() { ++idx; }
    double operator*() const {
      return data[(idx % MaxLength) * ElementLength + COL];
    }

  private:
    size_t idx;
    double *data;
  };

  template <size_t COL>
  column_accessor<COL> get_column_accessor(size_t N) const {
    return column_accessor<COL>(*this, N);
  }

  double get_first() const;

  std::array<double, ElementLength> get_earliest() const;

private:
  size_t index_; // 当前的插入位置
  std::array<std::array<double, ElementLength>, MaxLength>
      buffer_; // 缓冲区，存储固定长度为3的vector
public:
  bool is_full_; // 缓冲区是否已满
};

// 添加新值
template <size_t MaxLength, size_t ElementLength>
void CircularBuffer<MaxLength, ElementLength>::add(
    const std::array<double, ElementLength> &value) {
  buffer_[index_] = value;
  index_ = (index_ + 1) % MaxLength; // 循环更新索引

  // 缓冲区是否填满可以通过 index 的移动判断
  is_full_ = is_full_ || index_ == 0;
}

// 添加新值, double类型
template <size_t MaxLength, size_t ElementLength>
void CircularBuffer<MaxLength, ElementLength>::add(double value) {
  static_assert(ElementLength == 1,
                "add(double) can only be used when ElementLength is 1.");
  buffer_[index_][0] = value;
  index_ = (index_ + 1) % MaxLength;
  is_full_ = is_full_ || index_ == 0;
}

// 返回缓冲区中的所有值
template <size_t MaxLength, size_t ElementLength>
std::array<std::array<double, ElementLength>, MaxLength>
CircularBuffer<MaxLength, ElementLength>::get_all() const {
  std::array<std::array<double, ElementLength>, MaxLength> result;

  if (!is_full_) {
    std::copy(buffer_.begin(), buffer_.begin() + index_, result.begin());
  } else {
    std::copy(buffer_.begin() + index_, buffer_.end(), result.begin());
    std::copy(buffer_.begin(), buffer_.begin() + index_,
              result.begin() + (MaxLength - index_));
  }
  return result;
}

// template <size_t MaxLength, size_t ElementLength>
// Eigen::MatrixXd CircularBuffer<MaxLength, ElementLength>::get_all_eigen()
// const
// {
//     size_t num_rows = is_full_ ? MaxLength : index_;
//     Eigen::MatrixXd result(num_rows, ElementLength);
//     if (!is_full_)
//     {
//         for (size_t i = 0; i < index_; ++i)
//         {
//             for (size_t j = 0; j < ElementLength; ++j)
//             {
//                 result(i, j) = buffer_[i][j];
//             }
//         }
//     }
//     else
//     {
//         size_t row = 0;
//         for (size_t i = index_; i < MaxLength; ++i, ++row)
//         {
//             for (size_t j = 0; j < ElementLength; ++j)
//             {
//                 result(row, j) = buffer_[i][j];
//             }
//         }
//         for (size_t i = 0; i < index_; ++i, ++row)
//         {
//             for (size_t j = 0; j < ElementLength; ++j)
//             {
//                 result(row, j) = buffer_[i][j];
//             }
//         }
//     }
//     return result;
// }

// 获取最新的一个值
template <size_t MaxLength, size_t ElementLength>
std::array<double, ElementLength>
CircularBuffer<MaxLength, ElementLength>::get_latest() const {
  if (index_ == 0 && !is_full_) {
    throw std::runtime_error("Buffer is empty, no last value.");
  }

  size_t latest_index = (index_ == 0) ? MaxLength - 1 : index_ - 1;
  return buffer_[latest_index];
}

template <size_t MaxLength, size_t ElementLength>
double CircularBuffer<MaxLength, ElementLength>::get_last() const {
  if (index_ == 0 && !is_full_) {
    throw std::runtime_error("Buffer is empty, no latest value.");
  }

  size_t latest_index = (index_ == 0) ? MaxLength - 1 : index_ - 1;
  return buffer_[latest_index][0];
}

// 判断缓冲区是否为空
template <size_t MaxLength, size_t ElementLength>
bool CircularBuffer<MaxLength, ElementLength>::is_empty() const {
  return index_ == 0 && !is_full_;
}

template <size_t MaxLength, size_t ElementLength>
size_t CircularBuffer<MaxLength, ElementLength>::size() const {
  return is_full_ ? MaxLength : index_;
}

// 获取指定列的所有值
template <size_t MaxLength, size_t ElementLength>
std::array<double, MaxLength>
CircularBuffer<MaxLength, ElementLength>::get_column(
    size_t column_index) const {
  if (column_index >= ElementLength) {
    throw std::out_of_range("Column index is out of range.");
  }

  std::array<double, MaxLength> column_data;
  size_t row = 0;

  if (!is_full_) {
    for (size_t j = index_; j > 0; --j, ++row) {
      column_data[row] = buffer_[j - 1][column_index];
    }
  } else {
    // 缓冲区已满时, 从index - 1开始填充, 确保最新的在前
    size_t j = (index_ == 0) ? MaxLength - 1 : index_ - 1; // 当前最新的值的索引
    // 从最新数据开始填充
    for (; row < MaxLength; ++row) {
      column_data[row] = buffer_[j][column_index];
      j = (j == 0) ? MaxLength - 1 : j - 1; // 循环回绕
    }
  }
  return column_data;
}

// 获取最新的N个值
template <size_t MaxLength, size_t ElementLength>
std::vector<std::array<double, ElementLength>>
CircularBuffer<MaxLength, ElementLength>::get_latest_N(size_t N) const {
  size_t current_size = is_full_ ? MaxLength : index_;
  if (N > current_size) {
    throw std::out_of_range(
        "Requested number of elements exceeds the current buffer size.");
  }
  std::vector<std::array<double, ElementLength>> result;
  result.reserve(N);
  // 当缓冲区未满时，有效数据从 buffer_[0] 到
  // buffer_[index_-1]（逻辑顺序与物理顺序一致）
  // 当缓冲区已满时，有效数据逻辑顺序为：buffer_[(index_ + 0) % MaxLength] 到
  // buffer_[(index_ + MaxLength - 1) % MaxLength]
  for (size_t L = current_size - N; L < current_size; ++L) {
    size_t physical_index = is_full_ ? (index_ + L) % MaxLength : L;
    result.push_back(buffer_[physical_index]);
  }
  return result;
}

template <size_t MaxLength, size_t ElementLength>
std::array<std::array<double, ElementLength>, 3000>
CircularBuffer<MaxLength, ElementLength>::get_latest_3000() const {
  // 当前有效元素数量
  size_t current_size = is_full_ ? MaxLength : index_;
  // 若当前元素不足3000个, 抛出异常
  if (current_size < 3000) {
    throw std::out_of_range("缓冲区内不足3000个元素, 无法获取最新的3000个数据");
  }

  std::array<std::array<double, ElementLength>, 3000> result;

  // 数学推导:
  // 1. 当缓冲区未满时, 有效数据位于 buffer_[0] 到 buffer_[index_-1],
  // 逻辑顺序与物理顺序一致
  // 2. 当缓冲区已满时, 有效数据逻辑顺序:
  //    buffer_[(index_ + 0) % MaxLength], buffer_[(index_ + 1) % MaxLength],
  //    ..., buffer_[(index_ + MaxLength - 1) % MaxLength]
  // 3. 最新的3000个数据对应逻辑序号区间[current_size - 3000, current_size]
  //    对于每个逻辑序号 L, 物理索引计算为:
  //      physical_index = (is_full_ ? (index_ + L) % MaxLength : L)
  for (size_t L = current_size - 3000, outIndex = 0; L < current_size;
       ++L, ++outIndex) {
    size_t physical_index = is_full_ ? (index_ + L) % MaxLength : L;
    result[outIndex] = buffer_[physical_index];
  }
  return result;
}

template <size_t MaxLength, size_t ElementLength>
std::array<std::array<double, ElementLength>, 1000>
CircularBuffer<MaxLength, ElementLength>::get_latest_1000() const {
  // 当前有效元素数量
  size_t current_size = is_full_ ? MaxLength : index_;
  // 若当前元素不足1000个, 抛出异常
  if (current_size < 1000) {
    throw std::out_of_range("缓冲区内不足1000个元素, 无法获取最新的1000个数据");
  }

  std::array<std::array<double, ElementLength>, 1000> result;

  // 数学推导:
  // 1. 当缓冲区未满时, 有效数据位于 buffer_[0] 到 buffer_[index_-1],
  // 逻辑顺序与物理顺序一致
  // 2. 当缓冲区已满时, 有效数据逻辑顺序:
  //    buffer_[(index_ + 0) % MaxLength], buffer_[(index_ + 1) % MaxLength],
  //    ..., buffer_[(index_ + MaxLength - 1) % MaxLength]
  // 3. 最新的1000个数据对应逻辑序号区间[current_size - 1000, current_size]
  //    对于每个逻辑序号 L, 物理索引计算为:
  //      physical_index = (is_full_ ? (index_ + L) % MaxLength : L)
  for (size_t L = current_size - 1000, outIndex = 0; L < current_size;
       ++L, ++outIndex) {
    size_t physical_index = is_full_ ? (index_ + L) % MaxLength : L;
    result[outIndex] = buffer_[physical_index];
  }
  return result;
}

template <size_t MaxLength, size_t ElementLength>
double CircularBuffer<MaxLength, ElementLength>::get_first() const {
  static_assert(ElementLength == 1,
                "get_first() is just suitable for ElementLength 1");
  if (is_empty())
    throw std::runtime_error("Buffer is empty, no earliest value");
  size_t earliest_index = is_full_ ? index_ : 0;
  return buffer_[earliest_index][0];
}

template <size_t MaxLength, size_t ElementLength>
std::array<double, ElementLength>
CircularBuffer<MaxLength, ElementLength>::get_earliest() const {
  if (is_empty())
    throw std::runtime_error("Buffer is empty, no earliest value");
  size_t earliest_index = is_full_ ? index_ : 0;
  return buffer_[earliest_index];
}

template <size_t MaxLength, size_t ElementLength>
std::vector<double>
CircularBuffer<MaxLength, ElementLength>::get_latest_N_vec(size_t N) const {
  static_assert(ElementLength == 1,
                "get_latest_N_vec() can only be used when ElementLength is 1");
  size_t current_size = is_full_ ? MaxLength : index_;
  if (N > current_size)
    throw std::out_of_range(
        "Requested number of elements exceeds te current buffer size");

  std::vector<double> result;
  result.reserve(N);

  // 从最新的元素开始遍历
  for (size_t i = 0; i < N; ++i) {
    size_t logical_pos = current_size - N + i;
    size_t physical_pos =
        is_full_ ? (index_ + logical_pos) % MaxLength : logical_pos;
    result.push_back(
        buffer_[physical_pos][0]); // 因为 ElementLength=1, 直接取[0]
  }
  return result;
}