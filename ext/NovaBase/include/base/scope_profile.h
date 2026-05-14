#pragma once
#include "base_async_log.h"
#include "base_os_time.h"
#include <unordered_map>

#ifdef _SCOPE_PROFILE

#else
#define ScopeProfileWarm(tag, warm_up)
#define ScopeProfile(tag)
#define ScopeProfileReset()
#endif