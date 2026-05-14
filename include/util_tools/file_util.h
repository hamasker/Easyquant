#ifndef FILE_UTIL_FILE_UTIL_H_
#define FILE_UTIL_FILE_UTIL_H_

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

#include "common/rapidcsv.h"
#include "ryml_all.hpp"
#include "str_util.h"

namespace file_util {
// 文件类型枚举
enum class FileType {
  Regular,
  Directory,
  Symlink,
  Block,
  Character,
  FIFO,
  Socket,
  Unknown
};

// 文件权限枚举
enum class FilePermission {
  ReadOwner = 0400,
  WriteOwner = 0200,
  ExecOwner = 0100,
  ReadGroup = 0040,
  WriteGroup = 0020,
  ExecGroup = 0010,
  ReadOthers = 0004,
  WriteOthers = 0002,
  ExecOthers = 0001,
  All = 0777
};

// 文件信息结构体
struct FileInfo {
  std::string path;
  std::string filename;
  std::string extension;
  FileType type;
  std::streamsize size;
  std::chrono::system_clock::time_point last_write_time;
  std::uint32_t permissions;
};

/*| ----------------|
  |     文件操作     |
  |-----------------|*/

/**
 * @brief 检查文件或目录是否存在
 * @param path 文件路径
 * @return 是否存在
 */
inline bool FileExists(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec;
}

/**
 * @brief 检查路径是否为常规文件
 * @param path 文件路径
 * @return 是否为常规文件
 */
inline bool IsFile(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

/**
 * @brief 检查路径是否为目录
 * @param path 目录路径
 * @return 是否为目录
 */
inline bool IsDirectory(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

/**
 * @brief 检查目录是否为空
 * @param path 目录路径
 * @param include_hidden 是否包含隐藏文件（以.开头的文件）进行判断
 * @return 是否为空目录（若不是目录或无法访问，返回 false）
 */
inline bool IsDirectoryEmpty(const std::string &path,
                             bool include_hidden = false) noexcept {
  try {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      if (!include_hidden) {
        auto filename = entry.path().filename().string();
        if (!filename.empty() && filename[0] == '.')
          continue; // 忽略隐藏文件
      }
      return false; // 找到任意一个有效条目就不是空
    }
    return true; // 没有任何条目，说明为空
  } catch (...) {
    return false;
  }
}

/**
 * @brief 获取文件大小
 * @param path 文件路径
 * @return 文件大小(字节)，失败返回std::nullopt
 */
inline std::optional<std::uintmax_t>
GetFileSize(const std::string &path) noexcept {
  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  return ec ? std::nullopt : std::make_optional(size);
}

/**
 * @brief 获取文件扩展名
 * @param path 文件路径
 * @return 扩展名字符串(包含点)
 */
inline std::string GetFileExtension(const std::string &path) noexcept {
  try {
    return std::filesystem::path(path).extension().string();
  } catch (...) {
    return "";
  }
}

/**
 * @brief 获取文件名(不含扩展名)
 * @param path 文件路径
 * @return 文件名(不含扩展名)
 */
inline std::string GetFileStem(const std::string &path) noexcept {
  try {
    return std::filesystem::path(path).stem().string();
  } catch (...) {
    return "";
  }
}

/**
 * @brief 获取文件名(含扩展名)
 * @param path 文件路径
 * @return 文件名
 */
inline std::string GetFileName(const std::string &path) noexcept {
  try {
    return std::filesystem::path(path).filename().string();
  } catch (...) {
    return "";
  }
}

/**
 * @brief 获取父目录路径
 * @param path 文件路径
 * @return 父目录路径
 */
inline std::string GetParentPath(const std::string &path) noexcept {
  try {
    return std::filesystem::path(path).parent_path().string();
  } catch (...) {
    return "";
  }
}

/**
 * @brief 创建目录(包括所有必要父目录)
 * @param path 目录路径
 * @return 是否创建成功
 */
inline bool CreateDirectories(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::create_directories(path, ec) && !ec;
}

/**
 * @brief 创建单个目录
 * @param path 目录路径
 * @return 是否创建成功
 */
inline bool CreateDirectory(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::create_directory(path, ec) && !ec;
}

/**
 * @brief 删除文件或空目录
 * @param path 文件或目录路径
 * @return 是否删除成功
 */
inline bool Remove(const std::string &path) noexcept {
  std::error_code ec;
  return std::filesystem::remove(path, ec) && !ec;
}

/**
 * @brief 递归删除文件或目录
 * @param path 文件或目录路径
 * @return 删除的文件/目录数量
 */
inline std::uintmax_t RemoveAll(const std::string &path) noexcept {
  std::error_code ec;
  auto count = std::filesystem::remove_all(path, ec);
  return ec ? 0 : count;
}

/**
 * @brief 复制文件或目录
 * @param from 源路径
 * @param to 目标路径
 * @param overwrite 是否覆盖已存在文件
 * @return 是否复制成功
 */
inline bool Copy(const std::string &from, const std::string &to,
                 bool overwrite = false) noexcept {
  std::error_code ec;
  auto options = overwrite ? std::filesystem::copy_options::overwrite_existing
                           : std::filesystem::copy_options::none;
  std::filesystem::copy(from, to, options, ec);
  return !ec;
}

/**
 * @brief 重命名或移动文件/目录
 * @param old_path 原路径
 * @param new_path 新路径
 * @return 是否操作成功
 */
inline bool Rename(const std::string &old_path,
                   const std::string &new_path) noexcept {
  std::error_code ec;
  std::filesystem::rename(old_path, new_path, ec);
  return !ec;
}

/**
 * @brief 获取文件的最后修改时间
 * @param path 文件路径
 * @return 最后修改时间，失败返回std::nullopt
 */
inline std::optional<std::chrono::system_clock::time_point>
GetLastWriteTime(const std::string &path) noexcept {
  std::error_code ec;
  auto file_time = std::filesystem::last_write_time(path, ec);
  if (ec)
    return std::nullopt;

  // 手动转换时间
  auto duration_since_epoch = file_time.time_since_epoch();
  auto system_now = std::chrono::system_clock::now();
  auto file_now = decltype(file_time)::clock::now();
  return system_now +
         std::chrono::duration_cast<std::chrono::system_clock::duration>(
             duration_since_epoch - file_now.time_since_epoch());
}

/**
 * @brief 设置文件权限
 * @param path 文件路径
 * @param perms 权限标志
 * @return 是否设置成功
 */
inline bool SetPermissions(const std::string &path,
                           std::filesystem::perms perms) noexcept {
  std::error_code ec;
  std::filesystem::permissions(path, perms, ec);
  return !ec;
}

/**
 * @brief 获取文件类型
 * @param path 文件路径
 * @return 文件类型枚举
 */
inline FileType GetFileType(const std::string &path) noexcept {
  std::error_code ec;
  auto status = std::filesystem::status(path, ec);
  if (ec)
    return FileType::Unknown;

  if (std::filesystem::is_regular_file(status))
    return FileType::Regular;
  if (std::filesystem::is_directory(status))
    return FileType::Directory;
  if (std::filesystem::is_symlink(status))
    return FileType::Symlink;
  if (std::filesystem::is_block_file(status))
    return FileType::Block;
  if (std::filesystem::is_character_file(status))
    return FileType::Character;
  if (std::filesystem::is_fifo(status))
    return FileType::FIFO;
  if (std::filesystem::is_socket(status))
    return FileType::Socket;
  return FileType::Unknown;
}

/**
 * @brief 获取文件详细信息
 * @param path 文件路径
 * @return FileInfo结构体，失败时path为空
 */
inline FileInfo GetFileInfo(const std::string &path) noexcept {
  FileInfo info;
  std::error_code ec;

  auto status = std::filesystem::status(path, ec);
  if (ec)
    return info;

  info.path = path;
  info.filename = GetFileName(path);
  info.extension = GetFileExtension(path);
  info.type = GetFileType(path);
  info.size = std::filesystem::is_regular_file(status)
                  ? std::filesystem::file_size(path, ec)
                  : 0;
  if (ec)
    info.size = 0;

  auto file_time = std::filesystem::last_write_time(path, ec);
  if (!ec) {
    auto duration_since_epoch = file_time.time_since_epoch();
    auto system_now = std::chrono::system_clock::now();
    auto file_now = decltype(file_time)::clock::now();
    info.last_write_time =
        system_now +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            duration_since_epoch - file_now.time_since_epoch());
  }

  info.permissions = static_cast<std::uint32_t>(status.permissions());

  return info;
}

/**
 * @brief 获取当前工作目录
 * @return 当前工作目录路径
 */
inline std::string GetCurrentPath() noexcept {
  try {
    return std::filesystem::current_path().string();
  } catch (...) {
    return "";
  }
}

/**
 * @brief 设置当前工作目录
 * @param path 目录路径
 * @return 是否设置成功
 */
inline bool SetCurrentDirectory(const std::string &path) noexcept {
  std::error_code ec;
  std::filesystem::current_path(path, ec);
  return !ec;
}

/**
 * @brief 获取临时目录路径
 * @return 临时目录路径
 */
inline std::string GetTempDirectory() noexcept {
  try {
    return std::filesystem::temp_directory_path().string();
  } catch (...) {
    return "";
  }
}

/*|------------------|
  |     路径操作     |
  |------------------|*/

/**
 * @brief 连接路径组件
 * @param parts 路径组件列表
 * @return 连接后的路径
 */
inline std::string JoinPath(const std::vector<std::string> &parts) {
  std::filesystem::path result;
  for (const auto &part : parts) {
    if (!part.empty()) {
      result /= part;
    }
  }
  return result.string();
}

/**
 * @brief 连接路径组件（支持任意数量参数）
 * @tparam Args 路径组件类型（std::string, std::string_view, const char*等）
 * @param args 路径组件
 * @return 连接后的路径
 */
template <typename... Args> inline std::string JoinPath(const Args &...args) {
  std::filesystem::path result;
  (result /= ... /= std::filesystem::path(args));
  return result.string();
}

/**
 * @brief 规范化路径(处理./, ../等)
 * @param path 原始路径
 * @return 规范化后的路径
 */
inline std::string NormalizePath(const std::string &path) {
  try {
    return std::filesystem::canonical(path).string();
  } catch (...) {
    return std::filesystem::path(path).lexically_normal().string();
  }
}

/**
 * @brief 获取相对路径
 * @param path 原始路径
 * @param base 基础路径
 * @return 相对于base的路径
 */
inline std::string RelativePath(const std::string &path,
                                const std::string &base) {
  try {
    return std::filesystem::relative(path, base).string();
  } catch (...) {
    return path;
  }
}

/**
 * @brief 获取绝对路径
 * @param path 原始路径
 * @return 绝对路径
 */
inline std::string AbsolutePath(const std::string &path) {
  try {
    return std::filesystem::absolute(path).string();
  } catch (...) {
    return path;
  }
}

/**
 * @brief 检查路径是否为绝对路径
 * @param path 路径
 * @return 是否为绝对路径
 */
inline bool IsAbsolutePath(const std::string &path) noexcept {
  try {
    return std::filesystem::path(path).is_absolute();
  } catch (...) {
    return false;
  }
}

/*|------------------|
  |     文件读写     |
  |------------------|*/

/**
 * @brief 读取文本文件全部内容
 * @param path 文件路径
 * @return 文件内容
 * @throws std::runtime_error 如果读取失败
 */
inline std::string ReadTextFile(const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  const auto size = file.tellg();
  if (size == -1) {
    throw std::runtime_error("Failed to get file size: " + path);
  }

  file.seekg(0);
  std::string content(size, '\0');

  if (!file.read(content.data(), size)) {
    throw std::runtime_error("Failed to read file: " + path);
  }

  return content;
}

/**
 * @brief 逐行读取文本文件
 * @param path 文件路径
 * @return 包含所有行的vector
 * @throws std::runtime_error 如果读取失败
 */
inline std::vector<std::string> ReadLines(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(std::move(line));
  }

  if (file.bad()) {
    throw std::runtime_error("Error while reading file: " + path);
  }

  return lines;
}

/**
 * @brief 写入文本文件
 * @param path 文件路径
 * @param content 要写入的内容
 * @param create_dirs 是否自动创建目录
 * @throws std::runtime_error 如果写入失败
 */
inline void WriteTextFile(const std::string &path, std::string_view content,
                          bool create_dirs = true) {
  if (create_dirs) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
      CreateDirectories(dir.string());
    }
  }

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  if (!file.write(content.data(), content.size())) {
    throw std::runtime_error("Failed to write file: " + path);
  }
}

/**
 * @brief 逐行写入文本文件
 * @param path 文件路径
 * @param lines 要写入的行
 * @param create_dirs 是否自动创建目录
 * @throws std::runtime_error 如果写入失败
 */
inline void WriteLines(const std::string &path,
                       const std::vector<std::string> &lines,
                       bool create_dirs = true) {
  if (create_dirs) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
      CreateDirectories(dir.string());
    }
  }

  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  for (const auto &line : lines) {
    file << line << '\n';
  }

  if (file.bad()) {
    throw std::runtime_error("Error while writing file: " + path);
  }
}

/**
 * @brief 追加内容到文本文件
 * @param path 文件路径
 * @param content 要追加的内容
 * @param create_dirs 是否自动创建目录
 * @throws std::runtime_error 如果追加失败
 */
inline void AppendTextFile(const std::string &path, std::string_view content,
                           bool create_dirs = true) {
  if (create_dirs) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
      CreateDirectories(dir.string());
    }
  }

  std::ofstream file(path, std::ios::app | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file for appending: " + path);
  }

  if (!file.write(content.data(), content.size())) {
    throw std::runtime_error("Failed to append to file: " + path);
  }
}

inline void AppendLines(const std::string &path,
                        const std::vector<std::string> &lines) {
  std::ofstream file(path, std::ios::app);
  if (!file)
    throw std::runtime_error("Failed to open for appending");
  for (const auto &line : lines)
    file << line << '\n';
}

/**
 * @brief 读取二进制文件
 * @param path 文件路径
 * @return 包含文件内容的字节vector
 * @throws std::runtime_error 如果读取失败
 */
inline std::vector<uint8_t> ReadBinaryFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  const auto size = file.tellg();
  if (size == -1) {
    throw std::runtime_error("Failed to get file size: " + path);
  }

  file.seekg(0);
  std::vector<uint8_t> buffer(size);

  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    throw std::runtime_error("Failed to read file: " + path);
  }

  return buffer;
}

/**
 * @brief 写入二进制文件
 * @param path 文件路径
 * @param data 要写入的二进制数据
 * @param create_dirs 是否自动创建目录
 * @throws std::runtime_error 如果写入失败
 */
inline void WriteBinaryFile(const std::string &path,
                            const std::vector<uint8_t> &data,
                            bool create_dirs = true) {
  if (create_dirs) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
      CreateDirectories(dir.string());
    }
  }

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  if (!file.write(reinterpret_cast<const char *>(data.data()), data.size())) {
    throw std::runtime_error("Failed to write file: " + path);
  }
}

/*|------------------|
  |     文件枚举     |
  |------------------|*/

/**
 * @brief 列出目录中的文件
 * @param dir 目录路径
 * @param recursive 是否递归查找
 * @param filter 过滤函数(可选)
 * @return 文件路径列表
 * @throws std::runtime_error 如果枚举失败
 */
inline std::vector<std::string>
ListFiles(const std::string &dir, bool recursive = false,
          const std::function<bool(const std::filesystem::path &)> &filter =
              nullptr) {

  std::vector<std::string> files;
  try {
    if (recursive) {
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(dir)) {
        if (!filter || filter(entry.path())) {
          files.push_back(entry.path().string());
        }
      }
    } else {
      for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (!filter || filter(entry.path())) {
          files.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("Failed to list files: ") + e.what());
  }
  return files;
}

/**
 * @brief 列出目录中的文件(带扩展名过滤)
 * @param dir 目录路径
 * @param extensions 允许的扩展名列表(如 {".txt", ".csv"})
 * @param recursive 是否递归查找
 * @return 文件路径列表
 * @throws std::runtime_error 如果枚举失败
 */
inline std::vector<std::string>
ListFilesByExtension(const std::string &dir,
                     const std::vector<std::string> &extensions,
                     bool recursive = false) {

  std::vector<std::string> files;
  try {
    auto filter = [&extensions](const std::filesystem::path &path) {
      if (extensions.empty())
        return true;
      auto ext = path.extension().string();
      return std::find(extensions.begin(), extensions.end(), ext) !=
             extensions.end();
    };

    if (recursive) {
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(dir)) {
        if (filter(entry.path())) {
          files.push_back(entry.path().string());
        }
      }
    } else {
      for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (filter(entry.path())) {
          files.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("Failed to list files: ") + e.what());
  }
  return files;
}

/**
 * @brief 查找文件(递归)
 * @param dir 起始目录
 * @param filename 要查找的文件名
 * @return 找到的文件路径，未找到返回空字符串
 */
inline std::string FindFile(const std::string &dir,
                            const std::string &filename) {
  try {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(dir)) {
      if (entry.path().filename() == filename) {
        return entry.path().string();
      }
    }
  } catch (...) {
    // 忽略错误
  }
  return "";
}

/*|------------------|
  |     文件监控     |
  |------------------|*/

/**
 * @brief 监控文件变化的回调函数类型
 */
using FileWatchCallback =
    std::function<void(const std::string &path, bool is_dir)>;

/**
 * @brief 文件监控器类
 */
class FileWatcher {
public:
  FileWatcher() = default;
  ~FileWatcher() { Stop(); }

  /**
   * @brief 开始监控目录
   * @param path 要监控的路径
   * @param callback 变化回调
   * @param interval 检查间隔(毫秒)
   * @return 是否启动成功
   */
  bool Start(const std::string &path, FileWatchCallback callback,
             int interval = 1000) {
    if (running_)
      return false;

    try {
      path_ = std::filesystem::absolute(path).string();
      callback_ = std::move(callback);
      interval_ = std::chrono::milliseconds(interval);

      // 记录初始状态
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(path_)) {
        auto last_write = entry.last_write_time();
        file_times_[entry.path().string()] = last_write;
      }

      running_ = true;
      thread_ = std::thread(&FileWatcher::WatchLoop, this);
      return true;
    } catch (...) {
      return false;
    }
  }

  /**
   * @brief 停止监控
   */
  void Stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
    file_times_.clear();
  }

private:
  void WatchLoop() {
    while (running_) {
      try {
        std::vector<std::string> changed_files;

        // 检查现有文件变化
        for (auto it = file_times_.begin(); it != file_times_.end();) {
          std::error_code ec;
          auto new_time = std::filesystem::last_write_time(it->first, ec);

          if (ec) {
            // 文件被删除
            callback_(it->first, false);
            it = file_times_.erase(it);
          } else if (new_time != it->second) {
            // 文件被修改
            it->second = new_time;
            changed_files.push_back(it->first);
            ++it;
          } else {
            ++it;
          }
        }

        // 检查新文件
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(path_)) {
          auto path_str = entry.path().string();
          if (file_times_.find(path_str) == file_times_.end()) {
            file_times_[path_str] = entry.last_write_time();
            changed_files.push_back(path_str);
          }
        }

        // 触发回调
        for (const auto &path : changed_files) {
          callback_(path, std::filesystem::is_directory(path));
        }
      } catch (...) {
        // 忽略错误继续监控
      }

      std::this_thread::sleep_for(interval_);
    }
  }

  std::string path_;
  FileWatchCallback callback_;
  std::chrono::milliseconds interval_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::unordered_map<std::string, std::filesystem::file_time_type> file_times_;
};

/*|------------------|
  |     CSV操作     |
  |------------------|*/

/**
 * @brief 读取CSV文件
 * @tparam T 单元格数据类型(double/int/string等)
 * @param path 文件路径
 * @param delimiter 分隔符
 * @param skip_empty_lines 是否跳过空行
 * @param has_header 是否有标题行
 * @return 二维数据数组
 * @throws std::runtime_error 如果读取失败
 */
template <typename T = double>
inline std::vector<std::vector<T>>
ReadCsv(const std::string &path, char delimiter = ',',
        bool skip_empty_lines = true, bool has_header = false) {

  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }

  std::vector<std::vector<T>> data;
  std::string line;
  bool skipped_header = false;

  while (std::getline(file, line)) {
    if (skip_empty_lines && line.empty())
      continue;

    if (has_header && !skipped_header) {
      skipped_header = true;
      continue;
    }

    std::vector<T> row;
    std::stringstream ss(line);
    std::string cell;

    while (std::getline(ss, cell, delimiter)) {
      cell = str_util::StrTrim(cell);

      if constexpr (std::is_same_v<T, std::string>) {
        row.emplace_back(std::move(cell));
      } else {
        if (cell.empty()) {
          row.emplace_back(T{});
          continue;
        }

        try {
          if constexpr (std::is_same_v<T, double>) {
            row.emplace_back(std::stod(cell));
          } else if constexpr (std::is_same_v<T, float>) {
            row.emplace_back(std::stof(cell));
          } else if constexpr (std::is_same_v<T, int>) {
            row.emplace_back(std::stoi(cell));
          } else if constexpr (std::is_same_v<T, long>) {
            row.emplace_back(std::stol(cell));
          } else if constexpr (std::is_same_v<T, long long>) {
            row.emplace_back(std::stoll(cell));
          } else if constexpr (std::is_same_v<T, unsigned long>) {
            row.emplace_back(std::stoul(cell));
          } else if constexpr (std::is_same_v<T, unsigned long long>) {
            row.emplace_back(std::stoull(cell));
          } else {
            static_assert(std::is_arithmetic_v<T>, "Unsupported numeric type");
          }
        } catch (const std::exception &e) {
          throw std::runtime_error("Failed to convert cell '" + cell +
                                   "' in line: " + line + " - " + e.what());
        }
      }
    }

    data.emplace_back(std::move(row));
  }

  return data;
}

/**
 * @brief 写入CSV文件
 * @tparam T 单元格数据类型
 * @param path 文件路径
 * @param data 二维数据数组
 * @param delimiter 分隔符
 * @param header 标题行(可选)
 * @throws std::runtime_error 如果写入失败
 */
template <typename T>
inline void
WriteCsv(const std::string &path, const std::vector<std::vector<T>> &data,
         char delimiter = ',', const std::vector<std::string> &header = {}) {

  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open file for writing: " + path);
  }

  // 写入标题行
  if (!header.empty()) {
    for (size_t i = 0; i < header.size(); ++i) {
      file << header[i];
      if (i + 1 < header.size()) {
        file << delimiter;
      }
    }
    file << '\n';
  }

  // 写入数据
  for (const auto &row : data) {
    for (size_t i = 0; i < row.size(); ++i) {
      file << row[i];
      if (i + 1 < row.size()) {
        file << delimiter;
      }
    }
    file << '\n';
  }
}

/*|------------------|
  |     YAML操作    |
  |------------------|*/

/**
 * @brief 加载YAML文件
 * @param path 文件路径
 * @param tree 输出的YAML树
 * @return YAML根节点引用
 * @throws std::runtime_error 如果解析失败
 */
inline ryml::ConstNodeRef LoadYaml(const std::string &path, ryml::Tree &tree) {
  try {
    auto content = ReadTextFile(path);
    tree = ryml::parse_in_arena(ryml::to_csubstr(content));
    return tree.rootref();
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("YAML parse error: ") + e.what());
  }
}

/**
 * @brief 保存YAML到文件
 * @param path 文件路径
 * @param node YAML节点
 * @throws std::runtime_error 如果写入失败
 */
inline void SaveYaml(const std::string &path, const ryml::ConstNodeRef &node) {
  size_t buf_size = 4096;
  while (buf_size <= 16 * 1024 * 1024) {
    std::unique_ptr<char[]> buffer(new char[buf_size]);
    c4::substr out = c4::substr(buffer.get(), buf_size);
    try {
      out = ryml::emit_yaml(node, out);
      WriteTextFile(path,
                    std::string(out.str, out.len)); // 仍然需要拷贝一次用于写入
      return;
    } catch (...) {
      buf_size *= 2;
    }
  }
  throw std::runtime_error(
      "emit_yaml failed: YAML content too large or invalid");
}

inline void PrintYamlNode(const ryml::ConstNodeRef &node, int indent = 0) {
  const std::string indent_str(indent * 2, ' ');

  if (node.is_map()) {
    for (const auto &child : node.children()) {
      if (child.has_key() && child.has_val()) {
        std::cout << indent_str << child.key() << ": " << child.val() << "\n";
      } else if (child.has_key()) {
        std::cout << indent_str << child.key() << ":\n";
        PrintYamlNode(child, indent + 1);
      }
    }
  } else if (node.is_seq()) {
    for (const auto &child : node.children()) {
      if (child.has_val()) {
        std::cout << indent_str << "- " << child.val() << "\n";
      } else {
        std::cout << indent_str << "-\n";
        PrintYamlNode(child, indent + 1);
      }
    }
  } else if (node.has_val()) {
    std::cout << indent_str << node.val() << "\n";
  }
}

/**
 * @brief 从字符串解析 YAML
 * @param yaml_text YAML 文本内容
 * @param tree 解析出的 YAML 树
 * @return 根节点引用
 */
inline ryml::ConstNodeRef ParseYamlFromString(const std::string_view &yaml_text,
                                              ryml::Tree &tree) {
  tree = ryml::parse_in_arena(ryml::to_csubstr(yaml_text));
  return tree.rootref();
}

/**
 * @brief 将 YAML 节点转为字符串（不写入文件）
 * @param node YAML 节点
 * @return YAML 文本字符串
 */
inline std::string DumpYamlToString(const ryml::ConstNodeRef &node) {
  size_t buf_size = 4096;
  while (buf_size <= 16 * 1024 * 1024) {
    std::string buffer(buf_size, '\0');
    c4::substr out = c4::to_substr(buffer);
    try {
      out = ryml::emit_yaml(node, out);
      return std::string(out.str, out.len); // 剪切有效部分
    } catch (...) {
      buf_size *= 2;
    }
  }
  throw std::runtime_error("emit_yaml failed: content too large");
}

/**
 * @brief 构造空 YAML 树并返回根节点
 * @param tree 输出的 YAML 树
 * @return 根节点引用（可用于写入）
 */
inline ryml::NodeRef CreateEmptyYamlTree(ryml::Tree &tree) {
  tree.clear();
  return tree.rootref();
}

/**
 * @brief 从当前节点递归查找指定 key 的值
 * @param node 起始节点
 * @param key 目标键名
 * @param out_val 找到的值写入此参数
 * @return 找到的值
 */
template <typename T>
inline std::optional<T> RecursiveGetValue(const ryml::ConstNodeRef &node,
                                          std::string_view key) {
  if (node.invalid())
    return std::nullopt;

  if (node.is_map()) {
    for (auto child : node.children()) {
      if (child.has_key() && child.key() == ryml::to_csubstr(key) &&
          child.has_val()) {
        try {
          T val;
          child >> val;
          return val;
        } catch (...) {
          return std::nullopt;
        }
      }

      // 递归向下查找
      auto res = RecursiveGetValue<T>(child, key);
      if (res)
        return res;
    }
  } else if (node.is_seq()) {
    for (auto child : node.children()) {
      auto res = RecursiveGetValue<T>(child, key);
      if (res)
        return res;
    }
  }

  return std::nullopt;
}

template <typename T>
inline std::optional<T> GetValue(const ryml::Tree &tree, std::string_view key) {
  return file_util::RecursiveGetValue<T>(tree.rootref(), key);
}

/**
 * @brief 判断节点是否包含指定 key
 */
inline bool HasKey(const ryml::ConstNodeRef &node, std::string_view key) {
  return node.has_child(ryml::to_csubstr(key));
}

/**
 * @brief 获取字符串值（不存在返回默认）
 */
inline std::string GetString(const ryml::ConstNodeRef &node,
                             std::string_view key,
                             const std::string &default_val = "") {
  auto child = node.find_child(ryml::to_csubstr(key));
  if (child.invalid() || !child.has_val())
    return default_val;
  return std::string(child.val().str, child.val().len);
}

/**
 * @brief 获取 double 值（不存在返回默认）
 */
inline double GetDouble(const ryml::ConstNodeRef &node, std::string_view key,
                        double default_val = 0.0) {
  auto child = node.find_child(ryml::to_csubstr(key));
  if (child.invalid() || !child.has_val())
    return default_val;
  double val;
  child >> val;
  return val;
}

/**
 * @brief 获取 int 值（不存在返回默认）
 */
inline int GetInt(const ryml::ConstNodeRef &node, std::string_view key,
                  int default_val = 0) {
  auto child = node.find_child(ryml::to_csubstr(key));
  if (child.invalid() || !child.has_val())
    return default_val;
  int val;
  child >> val;
  return val;
}

/**
 * @brief 设置键值对（root 必须为 Map）
 */
inline void SetString(ryml::NodeRef &root, std::string_view key,
                      std::string_view value) {
  if (!root.is_map())
    root |= ryml::MAP;
  root[ryml::to_csubstr(key)] << ryml::to_csubstr(value);
}

inline void SetDouble(ryml::NodeRef &root, std::string_view key, double value) {
  if (!root.is_map())
    root |= ryml::MAP;
  root[ryml::to_csubstr(key)] << value;
}

inline void SetInt(ryml::NodeRef &root, std::string_view key, int value) {
  if (!root.is_map())
    root |= ryml::MAP;
  root[ryml::to_csubstr(key)] << value;
}

/*|------------------|
  |     其他操作     |
  |------------------|*/

/**
 * @brief 计算文件哈希(MD5)
 * @param path 文件路径
 * @return MD5哈希字符串
 * @throws std::runtime_error 如果计算失败
 */
// inline std::string CalculateFileHash(const std::string &path)
// {
//     std::ifstream file(path, std::ios::binary);
//     if (!file)
//     {
//         throw std::runtime_error("Failed to open file: " + path);
//     }

//     EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
//     const EVP_MD *md = EVP_md5();
//     unsigned char md_value[EVP_MAX_MD_SIZE];
//     unsigned int md_len;

//     EVP_DigestInit_ex(mdctx, md, nullptr);

//     constexpr size_t buffer_size = 4096;
//     char buffer[buffer_size];

//     while (file.read(buffer, buffer_size) || file.gcount())
//     {
//         EVP_DigestUpdate(mdctx, buffer, file.gcount());
//     }

//     EVP_DigestFinal_ex(mdctx, md_value, &md_len);
//     EVP_MD_CTX_free(mdctx);

//     std::stringstream ss;
//     ss << std::hex << std::setfill('0');
//     for (unsigned int i = 0; i < md_len; ++i)
//     {
//         ss << std::setw(2) << static_cast<unsigned>(md_value[i]);
//     }

//     return ss.str();
// }

/**
 * @brief 比较两个文件内容是否相同
 * @param path1 第一个文件路径
 * @param path2 第二个文件路径
 * @return 是否相同
 * @throws std::runtime_error 如果读取失败
 */
inline bool CompareFiles(const std::string &path1, const std::string &path2) {
  std::ifstream file1(path1, std::ios::binary | std::ios::ate);
  std::ifstream file2(path2, std::ios::binary | std::ios::ate);

  if (!file1 || !file2) {
    throw std::runtime_error("Failed to open files for comparison");
  }

  if (file1.tellg() != file2.tellg()) {
    return false;
  }

  file1.seekg(0);
  file2.seekg(0);

  constexpr size_t buffer_size = 4096;
  char buffer1[buffer_size], buffer2[buffer_size];

  while (file1.read(buffer1, buffer_size) && file2.read(buffer2, buffer_size)) {
    if (memcmp(buffer1, buffer2, buffer_size) != 0) {
      return false;
    }
  }

  // 比较剩余部分
  return memcmp(buffer1, buffer2, file1.gcount()) == 0;
}

/**
 * @brief 创建临时文件
 * @param prefix 文件名前缀
 * @param suffix 文件名后缀
 * @return 临时文件路径
 * @throws std::runtime_error 如果创建失败
 */
inline std::string CreateTempFile(std::string_view prefix = "",
                                  std::string_view suffix = "") {
  std::string temp_dir = GetTempDirectory();
  if (temp_dir.empty()) {
    throw std::runtime_error("Failed to get temp directory");
  }

  std::string pattern = JoinPath(
      {temp_dir, std::string(prefix) + "XXXXXX" + std::string(suffix)});

  // 使用mkstemp创建唯一文件名
  std::vector<char> pattern_vec(pattern.begin(), pattern.end());
  pattern_vec.push_back('\0');

  int fd = mkstemp(pattern_vec.data());
  if (fd == -1) {
    throw std::runtime_error("Failed to create temp file");
  }
  close(fd);

  return std::string(pattern_vec.begin(), pattern_vec.end() - 1);
}

/**
 * @brief 创建临时目录
 * @param prefix 目录名前缀
 * @return 临时目录路径
 * @throws std::runtime_error 如果创建失败
 */
inline std::string CreateTempDir(std::string_view prefix = "") {
  std::string temp_dir = GetTempDirectory();
  if (temp_dir.empty()) {
    throw std::runtime_error("Failed to get temp directory");
  }

  std::string pattern = JoinPath({temp_dir, std::string(prefix) + "XXXXXX"});

  // 使用mkdtemp创建唯一目录名
  std::vector<char> pattern_vec(pattern.begin(), pattern.end());
  pattern_vec.push_back('\0');

  if (mkdtemp(pattern_vec.data()) == nullptr) {
    throw std::runtime_error("Failed to create temp directory");
  }

  return std::string(pattern_vec.begin(), pattern_vec.end() - 1);
}
} // namespace file_util

#endif // FILE_UTIL_FILE_UTIL_H_