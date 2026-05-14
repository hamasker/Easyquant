#ifndef CALCULATE_UTIL_H
#define CALCULATE_UTIL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace calculate_util {
/*| ----------------|
  | 常规算术运算函数 |
  |-----------------|*/

// 单值指数
template <typename T> inline T VExp(T x) { return std::exp(x); }

// 单值自然对数（ln）
template <typename T> inline T VLogE(T x) {
  return std::log(x); // log_e(x)
}

// 单值 log2
template <typename T> inline T VLog2(T x) { return std::log2(x); }

// 单值 log10
template <typename T> inline T VLog10(T x) { return std::log10(x); }

// 单值绝对值
template <typename T> inline T VAbs(T x) { return std::abs(x); }

// 单值平方
template <typename T>
inline std::enable_if_t<std::is_arithmetic_v<T>, T> VSquare(T x) {
  return x * x;
}

// 单值平方根
template <typename T>
inline std::enable_if_t<std::is_arithmetic_v<T>, T> VSqrt(T x) {
  return std::sqrt(x);
}

// 单值符号函数
template <typename T> inline int VSign(T x) { return (T(0) < x) - (x < T(0)); }

// 四舍五入函数
inline double VRound(double value) { return std::round(value); }
inline double VRound(double value, int decimal_places) {
  double factor = std::pow(10.0, decimal_places);
  return VRound(value * factor) / factor;
}

/**
 * @brief 获取一个double数值的有效小数位数（基于字符串格式）
 * @param value 需要检查的值，比如0.01、0.0001
 * @return 小数点后有效位数（去除末尾0）
 */
inline int GetDecimalPrecision(double value) {
  if (value <= 0.0)
    return 0;

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(16) << value;
  std::string str = oss.str();

  auto pos = str.find('.');
  if (pos == std::string::npos)
    return 0;

  int precision = str.size() - pos - 1;

  // 去除末尾的 0
  while (precision > 0 && str.back() == '0') {
    str.pop_back();
    precision--;
  }

  return precision;
}

// 向上取整
template <typename T> inline T Ceil(T x) { return std::ceil(x); }

// 向下取整
template <typename T> inline T Floor(T x) { return std::floor(x); }

/*| ----------------|
|     计算操作     |
|-----------------|*/

// 通用平均值计算函数（支持vector和array）
template <typename Container>
auto CalMean(const Container &values) -> typename Container::value_type {
  if (values.empty())
    return typename Container::value_type(0);
  return std::accumulate(values.begin(), values.end(),
                         typename Container::value_type(0)) /
         static_cast<typename Container::value_type>(values.size());
}

// 通用中位数计算函数（支持vector和array）
template <typename Container>
auto CalMedian(Container values) -> typename Container::value_type {
  if (values.empty())
    throw std::domain_error("Cannot compute median of an empty container.");

  size_t n = values.size();
  size_t mid = n / 2;

  std::nth_element(values.begin(), values.begin() + mid, values.end());
  if (n % 2 == 1)
    return values[mid];
  else {
    auto upper = values[mid];
    std::nth_element(values.begin(), values.begin() + mid - 1,
                     values.begin() + mid);
    auto lower = values[mid - 1];
    return (lower + upper) / 2;
  }
}

// 计算 median 并返回 median 值和选择的索引
template <typename Container>
inline std::pair<double, size_t> CalMedianWithIndex(const Container &values) {
  if (values.empty()) {
    throw std::domain_error("Cannot compute median of an empty container.");
  }

  // 创建包含值和索引的 pair 数组
  std::vector<std::pair<double, size_t>> value_index_pairs;
  value_index_pairs.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    value_index_pairs.emplace_back(values[i], i);
  }

  // 按值排序，NaN 值排在最后
  std::sort(value_index_pairs.begin(), value_index_pairs.end(),
            [](const std::pair<double, size_t> &a,
               const std::pair<double, size_t> &b) {
              bool a_nan = !std::isfinite(a.first);
              bool b_nan = !std::isfinite(b.first);
              if (a_nan && b_nan)
                return false;
              if (a_nan)
                return false;
              if (b_nan)
                return true;
              return a.first < b.first;
            });

  size_t n = value_index_pairs.size();
  size_t mid = n / 2;

  double median_value;
  size_t median_index;

  if (n % 2 == 1) {
    median_value = value_index_pairs[mid].first;
    median_index = value_index_pairs[mid].second;
  } else {
    // 对于偶数个元素，median 是两个中间值的平均值
    median_value =
        (value_index_pairs[mid - 1].first + value_index_pairs[mid].first) / 2.0;
    // 选择最接近 median 值的索引（如果距离相等，选择较小的索引）
    double diff_lower = (value_index_pairs[mid - 1].first > median_value)
                            ? (value_index_pairs[mid - 1].first - median_value)
                            : (median_value - value_index_pairs[mid - 1].first);
    double diff_upper = (value_index_pairs[mid].first > median_value)
                            ? (value_index_pairs[mid].first - median_value)
                            : (median_value - value_index_pairs[mid].first);
    if (diff_lower <= diff_upper) {
      median_index = value_index_pairs[mid - 1].second;
    } else {
      median_index = value_index_pairs[mid].second;
    }
  }

  return {median_value, median_index};
}

/*| ----------------|
  | 通用算术运算函数 |
  |-----------------|*/

// 容器与标量的算术运算
template <typename Container, typename Op>
Container container_scalar_op(const Container &data,
                              typename Container::value_type scalar, Op op) {
  Container result;
  if constexpr (std::is_same_v<Container,
                               std::vector<typename Container::value_type>>) {
    result.reserve(data.size());
  }

  for (const auto &val : data) {
    if constexpr (std::is_same_v<Container,
                                 std::vector<typename Container::value_type>>) {
      result.push_back(op(val, scalar));
    } else {
      // 对于array的特殊处理
      for (size_t i = 0; i < data.size(); ++i)
        result[i] = op(data[i], scalar);
    }
  }
  return result;
}

// 两个容器间的算术运算
template <typename Container, typename Op>
Container container_op(const Container &a, const Container &b, Op op) {
  if (a.size() != b.size())
    throw std::invalid_argument("Container sizes do not match");

  Container result = a;
  for (size_t i = 0; i < a.size(); ++i)
    result[i] = op(a[i], b[i]);

  return result;
}

/*| ----------------|
  | 便捷操作函数 |
  |-----------------|*/

// 容器与标量的加减乘除
template <typename Container>
Container Add(const Container &data, typename Container::value_type value) {
  return container_scalar_op(data, value, std::plus<>());
}

template <typename Container>
Container Subtract(const Container &data,
                   typename Container::value_type value) {
  return container_scalar_op(data, value, std::minus<>());
}

template <typename Container>
Container Multiply(const Container &data,
                   typename Container::value_type value) {
  return container_scalar_op(data, value, std::multiplies<>());
}

template <typename Container>
Container Divide(const Container &data, typename Container::value_type value,
                 bool ignore_zero_division = false) {
  if (value == typename Container::value_type(0) && !ignore_zero_division) {
    throw std::runtime_error("Division by zero");
  }
  return container_scalar_op(data, value, std::divides<>());
}

// 两个容器间的加减乘除
template <typename Container>
Container Add(const Container &a, const Container &b) {
  return container_op(a, b, std::plus<>());
}

template <typename Container>
Container Subtract(const Container &a, const Container &b) {
  return container_op(a, b, std::minus<>());
}

template <typename Container>
Container Multiply(const Container &a, const Container &b) {
  return container_op(a, b, std::multiplies<>());
}

template <typename Container>
Container
Divide(const Container &a, const Container &b,
       bool ignore_zero_division = false,
       typename Container::value_type fallback =
           std::numeric_limits<typename Container::value_type>::quiet_NaN()) {
  using T = typename Container::value_type;
  auto safe_divide = [=](T x, T y) -> T {
    if (y == T(0)) {
      if (ignore_zero_division)
        return fallback;
      throw std::runtime_error("Division by zero");
    }
    return x / y;
  };
  return container_op(a, b, safe_divide);
}

template <typename Container, typename UnaryOp>
Container container_unary_op(const Container &data, UnaryOp op) {
  Container result = data;
  for (size_t i = 0; i < data.size(); ++i)
    result[i] = op(data[i]);
  return result;
}

// template <typename Container>
// Container Square(const Container &data)
// {
//     return container_unary_op(data, [](auto x)
//                               { return x * x; });
// }

template <typename Container> Container Sqrt(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::sqrt(x); });
}

template <typename Container> Container Ceil(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::ceil(x); });
}

template <typename Container> Container Floor(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::floor(x); });
}

template <typename Container> Container Exp(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::exp(x); });
}

template <typename Container> Container LogE(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::log(x); });
}

template <typename Container> Container Log2(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::log2(x); });
}

template <typename Container> Container Log10(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::log10(x); });
}

template <typename Container> Container Abs(const Container &data) {
  return container_unary_op(data, [](auto x) { return std::abs(x); });
}

inline std::vector<double> ExpDecayCoefficients(const int &span) {
  const double k = std::log(1 / 0.000001) / span;
  double decay = std::exp(-k);
  double exp_term = 1.0;
  std::vector<double> coefs(span);
  double sum = 0.0;
  for (int i = span - 1; i >= 0; --i) {
    coefs[i] = 0.5 + (1.0 - 0.5) * exp_term;
    sum += exp_term;
    exp_term *= decay;
  }
  for (int i = 0; i < span; ++i) {
    coefs[i] /= sum;
  }
  return coefs;
}

template <typename T>
inline T EMAStep(const T &prev, const T &currency, const double &alpha) {
  return (1 - alpha) * prev + alpha * currency;
}

template <typename T>
inline void EMAStepInplace(T &ema, const T &currency, const double &alpha) {
  ema = (1 - alpha) * ema + alpha * currency;
}

} // namespace calculate_util

#endif // CALCULATE_UTIL_H