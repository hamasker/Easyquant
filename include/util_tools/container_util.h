#ifndef CONTAINER_UTIL_H
#define CONTAINER_UTIL_H

#include <algorithm>
#include <deque>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <numeric> // iota
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility> // std::pair
#include <vector>

namespace container_util {

template <typename A, typename B>
std::ostream &operator<<(std::ostream &os, const std::pair<A, B> &p) {
  return os << "(" << p.first << ", " << p.second << ")";
}
// 通用容器打印函数（支持 vector/array/set/list 等）
template <typename Container>
void Print(const Container &c, const std::string &label = "",
           int precision = -1) {
  using std::begin;
  using std::end;

  if (!label.empty())
    std::cout << label << ": ";

  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);

  std::cout << "[";
  bool first = true;
  for (const auto &elem : c) {
    if (!first)
      std::cout << ", ";
    std::cout << elem;
    first = false;
  }
  std::cout << "]" << std::endl;

  // 恢复默认格式（可选）
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}

// 新增：支持直接打印数字类型
inline void Print(int value, const std::string &label = "",
                  int precision = -1) {
  if (!label.empty())
    std::cout << label << ": ";
  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);
  std::cout << value << std::endl;
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}
inline void Print(double value, const std::string &label = "",
                  int precision = -1) {
  if (!label.empty())
    std::cout << label << ": ";
  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);
  std::cout << value << std::endl;
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}
inline void Print(float value, const std::string &label = "",
                  int precision = -1) {
  if (!label.empty())
    std::cout << label << ": ";
  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);
  std::cout << value << std::endl;
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}
inline void Print(long value, const std::string &label = "",
                  int precision = -1) {
  if (!label.empty())
    std::cout << label << ": ";
  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);
  std::cout << value << std::endl;
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}
inline void Print(long long value, const std::string &label = "",
                  int precision = -1) {
  if (!label.empty())
    std::cout << label << ": ";
  if (precision >= 0)
    std::cout << std::fixed << std::setprecision(precision);
  std::cout << value << std::endl;
  if (precision >= 0) {
    std::cout.unsetf(std::ios::fixed);
    std::cout.precision(6);
  }
}

// 支持打印std::string，带label和双引号
inline void Print(const std::string &str, const std::string &label = "") {
  if (!label.empty())
    std::cout << label << ": ";
  std::cout << "\"" << str << "\"" << std::endl;
}
// 支持打印const char*，带label和双引号
inline void Print(const char *str, const std::string &label = "") {
  if (!label.empty())
    std::cout << label << ": ";
  std::cout << "\"" << (str ? str : "") << "\"" << std::endl;
}

// 支持打印bool类型，带label，输出true/false
inline void Print(bool value, const std::string &label = "") {
  if (!label.empty())
    std::cout << label << ": ";
  std::cout << (value ? "true" : "false") << std::endl;
}

// 支持多个参数同时打印，每个参数单独一行
template <typename T, typename... Args>
void Print(const T &first, const Args &...rest) {
  Print(first);
  if constexpr (sizeof...(rest) > 0) {
    Print(rest...);
  }
}

// 通用容器转字符串函数（支持 vector, array, list, set 等）
template <typename Container>
std::string ToString(const Container &container, int precision = -1) {
  std::ostringstream oss;
  if (precision >= 0)
    oss << std::fixed << std::setprecision(precision);

  oss << "{ ";
  bool first = true;
  for (const auto &elem : container) {
    if (!first)
      oss << ", ";
    oss << elem;
    first = false;
  }
  oss << " }";
  return oss.str();
}

// 泛型 PrintUMap，支持自定义 key 显示器
template <typename K, typename V, typename F = std::nullptr_t>
void PrintUMap(const std::unordered_map<K, V> &map,
               const std::string &title = "", F key_to_str = nullptr) {
  // 提前转成字符串用于格式对齐
  std::vector<std::pair<std::string, const V *>> printable;
  size_t max_key_len = 0;

  for (const auto &[key, val] : map) {
    std::string key_str;

    if constexpr (!std::is_same_v<F, std::nullptr_t>) {
      key_str = key_to_str(key);
    } else if constexpr (std::is_same_v<K, std::string>) {
      key_str = key;
    } else {
      static_assert(std::is_same_v<K, std::string>,
                    "非 string 类型请传入 key 转换函数");
    }

    max_key_len = std::max(max_key_len, key_str.length());
    printable.emplace_back(std::move(key_str), &val);
  }

  // 打印头部
  if (!title.empty())
    std::cout << title << " = {\n";
  else
    std::cout << "{\n";

  // 打印对齐数据
  for (const auto &[key_str, val_ptr] : printable) {
    std::cout << "  " << std::left << std::setw(static_cast<int>(max_key_len))
              << key_str << " => " << *val_ptr << "\n";
  }

  std::cout << "}\n";
}

template <typename K, typename V, typename F = std::nullptr_t>
std::string UMapToString(const std::unordered_map<K, V> &map,
                         const std::string &title = "",
                         F key_to_str = nullptr) {
  std::ostringstream oss;
  std::vector<std::pair<std::string, const V *>> printable;
  size_t max_key_len = 0;

  for (const auto &[key, val] : map) {
    std::string key_str;

    if constexpr (!std::is_same_v<F, std::nullptr_t>) {
      key_str = key_to_str(key);
    } else if constexpr (std::is_same_v<K, std::string>) {
      key_str = key;
    } else {
      static_assert(std::is_same_v<K, std::string>,
                    "非 string 类型请传入 key 转换函数");
    }

    max_key_len = std::max(max_key_len, key_str.length());
    printable.emplace_back(std::move(key_str), &val);
  }

  if (!title.empty())
    oss << title << " = {\n";
  else
    oss << "{\n";

  for (const auto &[key_str, val_ptr] : printable) {
    oss << "  " << std::left << std::setw(static_cast<int>(max_key_len))
        << key_str << " => " << *val_ptr << "\n";
  }

  oss << "}";
  return oss.str();
}

// 是否包含某个值（线性查找，O(n)）
// 适用于任何容器，无论是否排序
template <typename Container, typename T>
bool Find(const Container &container, const T &value) {
  return std::find(std::begin(container), std::end(container), value) !=
         std::end(container);
}

// 是否包含某个值（二分查找，O(log n)）
// ⚠️ 重要：要求容器必须已经排序（升序），否则结果是未定义的！
// 如果容器未排序，可能返回错误的结果（false negative 或 false positive）
// 使用前请确保容器已排序，例如：std::sort(container.begin(), container.end())
template <typename Container, typename T>
bool BFind(const Container &container, const T &value) {
  return std::binary_search(std::begin(container), std::end(container), value);
}

// map/unordered_map 的 key 查找专用 Find
template <typename K, typename V>
bool Find(const std::unordered_map<K, V> &map, const K &key) {
  return map.find(key) != map.end();
}

// 通用模板函数，获取 unordered_map 中所有的键
template <typename K, typename V>
std::vector<K> GetUMapKeys(const std::unordered_map<K, V> &map) {
  std::vector<K> keys;
  keys.reserve(map.size());
  for (const auto &pair : map)
    keys.push_back(pair.first);
  return keys;
}

// 提取 map 的所有值
template <typename K, typename V>
std::vector<V> GetUMapValues(const std::unordered_map<K, V> &map) {
  std::vector<V> values;
  values.reserve(map.size());
  for (const auto &[k, v] : map)
    values.push_back(v);
  return values;
}

// 去重（可选择原地或返回副本）
template <typename T>
std::vector<T> Unique(std::vector<T> &vec, bool inplace = true) {
  std::set<T> seen;
  if (inplace) {
    auto it = std::remove_if(vec.begin(), vec.end(), [&](const T &val) {
      return !seen.insert(val).second;
    });
    vec.erase(it, vec.end());
    return vec;
  } else {
    std::vector<T> result;
    result.reserve(vec.size());
    for (const auto &val : vec) {
      if (seen.insert(val).second)
        result.push_back(val);
    }
    return result;
  }
}

// 通用排序：is_asc 控制升/降序，inplace 控制是否修改原始容器
template <typename T>
std::vector<T> Sort(std::vector<T> &vec, bool is_asc = true,
                    bool inplace = true) {
  if (inplace) {
    if (is_asc)
      std::sort(vec.begin(), vec.end());
    else
      std::sort(vec.begin(), vec.end(), std::greater<T>());
    return vec;
  } else {
    std::vector<T> result = vec;
    if (is_asc)
      std::sort(result.begin(), result.end());
    else
      std::sort(result.begin(), result.end(), std::greater<T>());
    return result;
  }
}

// 获取最大值索引
template <typename T> size_t ArgMax(const std::vector<T> &vec) {
  return std::distance(vec.begin(), std::max_element(vec.begin(), vec.end()));
}

// 获取最小值索引
template <typename T> size_t ArgMin(const std::vector<T> &vec) {
  return std::distance(vec.begin(), std::min_element(vec.begin(), vec.end()));
}

// 清空容器（swap 技巧）
template <typename T> void FastClear(T &container) { T().swap(container); }

// 合并（inplace: true 合并到 a，否则返回新 vector）
template <typename T>
std::vector<T> Merge(std::vector<T> &a, const std::vector<T> &b,
                     bool inplace = true) {
  if (inplace) {
    a.reserve(a.size() + b.size());
    a.insert(a.end(), b.begin(), b.end());
    return a;
  } else {
    std::vector<T> result;
    result.reserve(a.size() + b.size());
    result.insert(result.end(), a.begin(), a.end());
    result.insert(result.end(), b.begin(), b.end());
    return result;
  }
}

// vector 转 set
template <typename T>
std::set<T> ToSet(std::vector<T> &vec, bool inplace = true) {
  if (inplace) {
    vec = Unique(vec, true);
    return std::set<T>(vec.begin(), vec.end());
  } else {
    auto tmp = Unique(vec, false);
    return std::set<T>(tmp.begin(), tmp.end());
  }
}

// set 转 vector（无实际 in-place 操作意义，但统一接口）
template <typename T>
std::vector<T> ToVector(const std::set<T> &s,
                        [[maybe_unused]] bool inplace = true) {
  return std::vector<T>(s.begin(), s.end());
}

// map 排序：by_value 控制按值还是按键，is_asc 控制升降序
template <typename K, typename V>
std::vector<std::pair<K, V>> SortMap(std::unordered_map<K, V> &map,
                                     bool by_value = true,
                                     bool is_asc = false) {
  std::vector<std::pair<K, V>> vec(map.begin(), map.end());
  if (by_value) {
    if (is_asc)
      std::sort(vec.begin(), vec.end(), [](const auto &a, const auto &b) {
        return a.second < b.second;
      });
    else
      std::sort(vec.begin(), vec.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
      });
  } else {
    if (is_asc)
      std::sort(vec.begin(), vec.end(),
                [](const auto &a, const auto &b) { return a.first < b.first; });
    else
      std::sort(vec.begin(), vec.end(),
                [](const auto &a, const auto &b) { return a.first > b.first; });
  }
  return vec;
}

// 二分查找（返回是否找到）
template <typename T>
bool BinaryFind(const std::vector<T> &sorted_vec, const T &target) {
  return std::binary_search(sorted_vec.begin(), sorted_vec.end(), target);
}

// 构造 iota 向量：0,1,2,...,n-1
inline std::vector<int> Iota(int n) {
  std::vector<int> vec(n);
  std::iota(vec.begin(), vec.end(), 0);
  return vec;
}
} // namespace container_util

#endif // CONTAINER_UTIL_H
