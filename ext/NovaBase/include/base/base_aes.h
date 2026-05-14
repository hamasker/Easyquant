#pragma once

#include "base/base_util.h"

BEGIN_NOVA_NAMESPACE(base)

struct AES_ctx;

AES_ctx *AES_create_ctx(const uint8_t *key, const uint8_t *iv);

void AES_CBC_encrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, size_t length);
void AES_CBC_decrypt_buffer(struct AES_ctx *ctx, uint8_t *buf, size_t length);

END_NOVA_NAMESPACE(base)