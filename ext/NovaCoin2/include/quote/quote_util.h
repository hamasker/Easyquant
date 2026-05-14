#pragma once

#include <base/base_config.h>
#include <base/base_hash.h>

// #include "quote_struct.h"

BEGIN_NOVA_NAMESPACE(quote)

struct CoinCharVal {
  static int Get(char ch) {
    if ('0' <= ch && ch <= '9')
      return ch - '0';
    else if ('a' <= ch && ch <= 'z')
      return ch - 'a' + 10;
    else if (ch == '.')
      return 36;
    else if (ch == '-')
      return 37;
    else if (ch == '_')
      return 38;
    return -1;
  }
  static int Char(int x) {
    if (x < 0)
      return '?';
    else if (x < 10)
      return '0' + x;
    else if (x < 36)
      return 'a' + x - 10;
    else if (x == 36)
      return '.';
    else if (x == 37)
      return '-';
    else if (x == 38)
      return '_';
    return '?';
  }
  static constexpr int CharSetN = 39;
};

END_NOVA_NAMESPACE(quote)