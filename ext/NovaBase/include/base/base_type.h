#pragma once

#include "base/base_common.h"

BEGIN_NOVA_NAMESPACE(base)

enum ServerType {
  SERVER_TYPE_INIT = 0,
  SERVER_TYPE_TRADE,
  SERVER_TYPE_QUOTE,
  SERVER_TYPE_AUX,
  SERVER_TYPE_UNKNOWN
};

union version_t {
  uint32_t u32;
  struct {
    uint32_t patch2 : 8;
    uint32_t patch1 : 8;
    uint32_t minor : 8;
    uint32_t major : 8;
  };
};

union session_id_t {
  uint64_t u64;
  struct {
    uint64_t seq : 32;
    uint64_t svr_id : 8;
    uint64_t reserved : 24;
  };
};

using user_id_t = uint16_t;
using client_id_t = uint8_t;

struct server_info_t {
  int32_t id;
  version_t version;
  char name[32];
  ServerType type;
};

struct client_info_t {
  version_t version;
  user_id_t user_id;
  client_id_t client_id;
  uint8_t reserved;
};

// template <typename T, T... Ints>
// using IntegerSequence = std::integer_sequence<T, Ints...>;

// template <std::size_t... Ints>
// using IndexSequence = std::index_sequence<Ints...>;

// template <typename T, T N>
// using MakeIntegerSequence = std::make_integer_sequence<T, N>;

// template <std::size_t N>
// using MakeIndexSequence = std::make_index_sequence<N>;

// template <typename... Ts>
// using IndexSequenceFor = std::index_sequence_for<Ts...>;

class noncopyable {
public:
  noncopyable(const noncopyable &) = delete;
  void operator=(const noncopyable &) = delete;

protected:
  noncopyable() = default;
  ~noncopyable() = default;
};

END_NOVA_NAMESPACE(base)