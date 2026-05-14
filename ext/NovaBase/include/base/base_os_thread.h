#pragma once

#include "base/base_util.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

BEGIN_NOVA_NAMESPACE(base)
using NovaThread = std::thread;
using NovaThreadManager = std::vector<NovaThread *>;

#if _IS_WINDOWS
#else
typedef pid_t os_pid_t;
typedef pid_t os_tid_t;
typedef void *os_tec_t;
typedef pthread_t os_thread_t;
#endif

#if _IS_WINDOWS
#else
#define Prefetch(_macro_addr, _macro_hint)                                     \
  _mm_prefetch((_macro_addr), (_macro_hint))
#endif

bool BindCore(uint32_t cpiId);

END_NOVA_NAMESPACE(base)