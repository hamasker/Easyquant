#pragma once

#include "base/base_util.h"
#include "base/base_os_io.h"

#ifdef _WIN32

#else

#include <sys/mman.h>
#endif

BEGIN_NOVA_NAMESPACE(base)
END_NOVA_NAMESPACE(base)