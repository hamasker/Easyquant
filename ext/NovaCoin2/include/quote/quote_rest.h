#ifndef NOVACOIN_QUOTE_ENGINE_REST_GET_H_
#define NOVACOIN_QUOTE_ENGINE_REST_GET_H_

#pragma once

#include "nova_base.h"

BEGIN_NOVA_NAMESPACE(quote)

void curl_api(const std::string &url, std::string &result_json,
              const std::string &action = "GET");

END_NOVA_NAMESPACE(quote)

#endif // NOVACOIN_QUOTE_ENGINE_REST_GET_H_
