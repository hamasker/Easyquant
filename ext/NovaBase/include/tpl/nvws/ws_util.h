#pragma once

#include "ws_struct.h"

BEGIN_NOVA_NAMESPACE(ws)

bool gz_decompress(char *out_dst, uint64_t *out_dst_len, const char *src, uint64_t src_len);

bool SSL_env_init();

END_NOVA_NAMESPACE(ws)