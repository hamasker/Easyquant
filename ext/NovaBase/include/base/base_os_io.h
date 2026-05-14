#pragma once

#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(base)

using os_file_t = int;
using os_mode_t = mode_t;
using os_dir_t = DIR *;
using os_dirent_t = dirent;

#define OS_FILE_NULL (-1)
#define OS_DIR_NULL NULL

END_NOVA_NAMESPACE(base)