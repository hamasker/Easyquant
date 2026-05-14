#pragma once

#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(base)

int32_t Base64Encode(char *encoded, const char *plain, int32_t len);

int32_t Base64Decode(char *decoded, const char *encoded, int32_t len);

END_NOVA_NAMESPACE(base)