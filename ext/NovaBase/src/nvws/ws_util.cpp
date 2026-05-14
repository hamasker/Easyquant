#include "nvws/ws_util.h"
#include <cstring>
#include <mutex>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <zlib.h>

BEGIN_NOVA_NAMESPACE(ws)

/**
 * gzip解压缩函数
 * @param out_dst 输出缓冲区
 * @param out_dst_len
 * 输出缓冲区长度（输入时是最大长度，输出时是实际解压后的长度）
 * @param src 输入压缩数据
 * @param src_len 输入数据长度
 * @return 解压成功返回true，否则返回false
 */
bool gz_decompress(char *out_dst, uint64_t *out_dst_len, const char *src,
                   uint64_t src_len) {
  if (!out_dst || !out_dst_len || !src || src_len == 0) {
    return false;
  }

  z_stream strm;
  std::memset(&strm, 0, sizeof(strm));

  // 初始化zlib解压缩
  // windowBits = 15 + 16 表示使用gzip格式
  if (inflateInit2(&strm, 15 + 16) != Z_OK) {
    return false;
  }

  strm.avail_in = static_cast<uInt>(src_len);
  strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src));
  strm.avail_out = static_cast<uInt>(*out_dst_len);
  strm.next_out = reinterpret_cast<Bytef *>(out_dst);

  // 执行解压缩
  int ret = inflate(&strm, Z_FINISH);

  bool success = false;
  if (ret == Z_STREAM_END) {
    *out_dst_len = strm.total_out;
    success = true;
  } else if (ret == Z_OK) {
    // 输出缓冲区可能不够大
    *out_dst_len = strm.total_out;
    success = false;
  } else {
    success = false;
  }

  // 清理
  inflateEnd(&strm);
  return success;
}

/**
 * SSL环境初始化函数
 * 确保OpenSSL库被正确初始化，只会初始化一次
 * @return 初始化成功返回true
 */
bool SSL_env_init() {
  static std::once_flag init_flag;
  static bool init_result = false;

  std::call_once(init_flag, []() {
    // OpenSSL 1.1.0+ 会自动初始化，但为了兼容性，我们显式初始化
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    init_result = true;
  });

  return init_result;
}

END_NOVA_NAMESPACE(ws)
