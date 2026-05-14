#pragma once

#include "base/nova_base.h"

BEGIN_NOVA_NAMESPACE(base)

class BigInteger {
public:
  static constexpr int MAX_DIGITS = 16;
  static constexpr int64_t BI_BASE = 10000ll;

public:
  BigInteger(int64_t x) {}

  BigInteger(const BigInteger &other) {}

  int64_t ToInt64() const {
    if (n_ <= 0)
      return 0;
    int64_t ret = 0;
    for (int i = n_ - 1; i >= 0; i--) {
      ret = ret * BI_BASE + digits_[i];
    }
    return ret * sign_;
  }

  //   double ToDouble() const {}

  //   std::string ToStringRaw() const {}

  //   std::string ToString() const {}

public:
  void Plus(const BigInteger &other) {}

  void Minus(const BigInteger &other) {}

  void MultiPow(const BigInteger &other) {}

  void DivConst(const BigInteger &other) {}

protected:
  void SignPlus(const BigInteger &other) {}

  void SignMinus(const BigInteger &other) {}

protected:
  int sign_ = 1;
  int n_ = 0;
  int digits_[30];
} NOVA_ALIGNED(64);

END_NOVA_NAMESPACE(base)
