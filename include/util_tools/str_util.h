#ifndef STR_UTIL_H
#define STR_UTIL_H

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace str_util {
/*| ----------------|
  |    字符串操作    |
  |-----------------|*/

/**
 * @brief 检查字符串是否包含子串
 * @param str 目标字符串
 * @param substr 要查找的子串
 * @return 是否包含
 */
inline bool StrContains(std::string_view str, std::string_view substr) {
  return str.find(substr) != std::string_view::npos;
}

/**
 * @brief 比较两个字符串是否相等
 * @param a 第一个字符串
 * @param b 第二个字符串
 * @param ignore_case 是否忽略大小写
 * @return 是否相等
 */
inline bool StrEqual(std::string_view a, std::string_view b,
                     bool ignore_case = false) {
  if (a.size() != b.size())
    return false;
  if (!ignore_case)
    return a == b;

  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 将字符串转换为大写(原地修改)
 * @param src 要转换的字符串
 */
inline void StrUpper(std::string &src) {
  for (auto &ch : src)
    ch = std::toupper(static_cast<unsigned char>(ch));
}

/**
 * @brief 将字符串转换为大写(返回新字符串)
 * @param str 要转换的字符串
 * @return 转换后的字符串
 */
inline std::string StrUpper(std::string_view str) {
  std::string ret;
  ret.reserve(str.size());
  for (char ch : str)
    ret.push_back(std::toupper(static_cast<unsigned char>(ch)));
  return ret;
}

/**
 * @brief 将字符串转换为小写(原地修改)
 * @param src 要转换的字符串
 */
inline void StrLower(std::string &src) {
  for (auto &ch : src)
    ch = std::tolower(static_cast<unsigned char>(ch));
}

/**
 * @brief 将字符串转换为小写(返回新字符串)
 * @param str 要转换的字符串
 * @return 转换后的字符串
 */
inline std::string StrLower(std::string_view str) {
  std::string ret;
  ret.reserve(str.size());
  for (char ch : str)
    ret.push_back(std::tolower(static_cast<unsigned char>(ch)));
  return ret;
}

/**
 * @brief 替换字符串中的子串(原地修改)
 * @tparam T 字符串类型(std::string)
 * @param str 目标字符串
 * @param from 要被替换的子串
 * @param to 替换为的子串
 * @return 是否进行了替换
 */
template <typename T>
inline std::enable_if_t<std::is_same_v<T, std::string>, bool>
StrReplace(T &str, std::string_view from, std::string_view to) {
  if (from.empty())
    return false;

  bool found = false;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    found = true;
    start_pos += to.length();
  }
  return found;
}

/**
 * @brief 替换字符串中的子串(返回新字符串)
 * @param str 目标字符串
 * @param from 要被替换的子串
 * @param to 替换为的子串
 * @return 替换后的字符串
 */
inline std::string StrReplace(std::string_view str, std::string_view from,
                              std::string_view to) {
  if (from.empty())
    return std::string(str);

  std::string result(str);
  size_t start_pos = 0;
  while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
    result.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return result;
}

/**
 * @brief 连接字符串列表
 * @tparam Container 容器类型
 * @param parts 字符串列表
 * @param delimiter 分隔符
 * @return 连接后的字符串
 */
template <typename Container>
inline std::string StrJoin(const Container &parts, std::string_view delimiter) {
  if (parts.empty())
    return "";

  size_t total_size = 0;
  for (const auto &part : parts)
    total_size += part.size() + delimiter.size();

  std::string result;
  result.reserve(total_size);
  auto it = parts.begin();
  result += *it;

  for (++it; it != parts.end(); ++it) {
    result += delimiter;
    result += *it;
  }
  return result;
}

/**
 * @brief 检查字符串是否以特定前缀开始
 * @param str 目标字符串
 * @param prefix 前缀
 * @return 是否匹配
 */
inline bool StartsWith(std::string_view str, std::string_view prefix) {
  return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

/**
 * @brief 检查字符串是否以特定后缀结束
 * @param str 目标字符串
 * @param suffix 后缀
 * @return 是否匹配
 */
inline bool EndsWith(std::string_view str, std::string_view suffix) {
  return str.size() >= suffix.size() &&
         str.substr(str.size() - suffix.size()) == suffix;
}

/**
 * @brief 按单个分隔符分割字符串
 * @param str 目标字符串
 * @param delimiter 分隔符
 * @return 分割后的字符串列表
 */
inline std::vector<std::string> StrSplit(std::string_view str, char delimiter) {
  std::vector<std::string> tokens;
  size_t start = 0, end = 0;
  while ((end = str.find(delimiter, start)) != std::string_view::npos) {
    tokens.emplace_back(str.substr(start, end - start));
    start = end + 1;
  }
  tokens.emplace_back(str.substr(start));
  return tokens;
}

/**
 * @brief 按多个分隔符分割字符串
 * @param str 目标字符串
 * @param delims 分隔符集合
 * @param skip_empty 是否跳过空字符串
 * @return 分割后的字符串列表
 */
inline std::vector<std::string> StrSplit(std::string_view str,
                                         std::string_view delims = ",",
                                         bool skip_empty = true) {
  std::vector<std::string> tokens;
  size_t prev = 0, pos = 0;
  do {
    pos = str.find_first_of(delims, prev);
    if (pos == std::string_view::npos)
      pos = str.length();

    std::string token(str.substr(prev, pos - prev));
    if (!token.empty() || !skip_empty)
      tokens.push_back(std::move(token));

    prev = pos + 1;
  } while (pos < str.length() && prev < str.length());
  return tokens;
}

/**
 * @brief 去除字符串前后空白字符
 * @param str 目标字符串
 * @return 处理后的字符串
 */
inline std::string StrTrim(std::string_view str) {
  auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) {
    return std::isspace(ch);
  });
  auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
               return std::isspace(ch);
             }).base();
  return (start < end) ? std::string(start, end) : std::string();
}

/**
 * @brief 去除字符串前后空白字符(原地修改)
 * @param str 目标字符串
 */
inline void StrTrimInPlace(std::string &str) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_space));
  str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(),
            str.end());
};

/**
 * @brief 去除字符串特定前缀
 * @param input 目标字符串
 * @param prefix 要去除的前缀
 * @return 处理后的字符串
 */
inline std::string StrRemovePrefix(std::string_view input,
                                   std::string_view prefix) {
  if (input.length() >= prefix.length() &&
      input.substr(0, prefix.length()) == prefix)
    return std::string(input.substr(prefix.length()));
  return std::string(input);
}

/**
 * @brief 去除字符串特定后缀
 * @param input 目标字符串
 * @param suffix 要去除的后缀
 * @return 处理后的字符串
 */
inline std::string StrRemoveSuffix(std::string_view input,
                                   std::string_view suffix) {
  if (input.length() >= suffix.length() &&
      input.substr(input.length() - suffix.length()) == suffix)
    return std::string(input.substr(0, input.length() - suffix.length()));
  return std::string(input);
}

/**
 * @brief 统计子串出现次数
 * @param str 目标字符串
 * @param substr 要统计的子串
 * @return 出现次数
 */
inline size_t StrCount(std::string_view str, std::string_view substr) {
  if (substr.empty())
    return 0;

  size_t count = 0;
  size_t pos = 0;
  while ((pos = str.find(substr, pos)) != std::string_view::npos) {
    ++count;
    pos += substr.length();
  }
  return count;
}

/**
 * @brief 打印 std::string 到控制台，带可选标签
 * @param str 要打印的字符串
 * @param label 标签（可选）
 */
inline void StrPrint(const std::string &str, const std::string &label = "") {
  if (!label.empty())
    std::cout << label << ": ";

  std::cout << "\"" << str << "\"" << std::endl;
}
} // namespace str_util

#endif // STR_UTIL_H