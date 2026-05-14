#pragma once

#include "base/base_util.h"

#include "base/base_login.h"
#include "base/base_service.h"
#include "base/base_session_manager.h"
#include "base/base_tcp_client.h"
#include "base/base_tcp_server.h"

#include <unordered_map>
#include <utility>

BEGIN_NOVA_NAMESPACE(base)

static constexpr auto NOVA_HEARTBEAT_S = 10;

class NovaRecvManager {
  struct CB {
    RecvFunc cb;
    std::string name;
  };

public:
  NovaRecvManager(const char *ip, uint16_t prot, server_info_t si);
  ~NovaRecvManager();

  bool Initialize();
  void Destroy();

  bool AddService(uint16_t code, RecvFunc cb);
};
END_NOVA_NAMESPACE(base)