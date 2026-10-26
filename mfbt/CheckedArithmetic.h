/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Provides checked integers operations, detecting overflow. */

#ifndef mozilla_CheckedArithmetic_h
#define mozilla_CheckedArithmetic_h

#ifdef _MSC_VER
#include <bit>
#include <limits>
#include <type_traits>
#endif

namespace mozilla {

#ifdef _MSC_VER
// MSVC doesn't support GCC's checked arithmetic builtins. Compute the results
// in the corresponding unsigned type (which is well defined even when it
// wraps) and re-interpret them to detect overflow.

namespace detail {

template <typename T>
constexpr bool SafeAddUnsignedWrap(T lhs, T rhs, T* res) {
  using UnsignedT = std::make_unsigned_t<T>;
  UnsignedT unsignedResult =
      static_cast<UnsignedT>(lhs) + static_cast<UnsignedT>(rhs);
  T result = std::bit_cast<T>(unsignedResult);
  // Overflow if the operands have the same sign and the result's sign
  // differs.
  if ((lhs >= 0) == (rhs >= 0) && (result >= 0) != (lhs >= 0)) {
    return false;
  }
  *res = result;
  return true;
}

template <typename T>
constexpr bool SafeSubUnsignedWrap(T lhs, T rhs, T* res) {
  using UnsignedT = std::make_unsigned_t<T>;
  UnsignedT unsignedResult =
      static_cast<UnsignedT>(lhs) - static_cast<UnsignedT>(rhs);
  T result = std::bit_cast<T>(unsignedResult);
  // Overflow if the operands' signs differ and the result's sign differs
  // from the left-hand side's sign.
  if ((lhs >= 0) != (rhs >= 0) && (result >= 0) != (lhs >= 0)) {
    return false;
  }
  *res = result;
  return true;
}

template <typename T>
constexpr bool SafeMulUnsignedWrap(T lhs, T rhs, T* res) {
  if (lhs == 0 || rhs == 0) {
    *res = 0;
    return true;
  }
  constexpr T kMinValue = std::numeric_limits<T>::min();
  if (lhs == kMinValue || rhs == kMinValue) {
    // The minimum value times -1 overflows, and its magnitude exceeds that
    // of any other value, so any other non-zero factor overflows as well.
    // Multiplying the minimum value by one is fine.
    if ((lhs == 1 && rhs == kMinValue) || (rhs == 1 && lhs == kMinValue)) {
      *res = kMinValue;
      return true;
    }
    return false;
  }
  using UnsignedT = std::make_unsigned_t<T>;
  UnsignedT unsignedResult =
      static_cast<UnsignedT>(lhs) * static_cast<UnsignedT>(rhs);
  T result = std::bit_cast<T>(unsignedResult);
  // A non-zero exact result never wraps to zero.
  if (result == 0) {
    return false;
  }
  // If no overflow occurred, dividing the wrapped result by one of the
  // operands recovers the other one.
  if (result / lhs != rhs) {
    return false;
  }
  *res = result;
  return true;
}

}  // namespace detail

template <typename T>
[[nodiscard]] constexpr bool SafeAdd(T lhs, T rhs, T* res) {
  if constexpr (std::is_signed_v<T>) {
    return detail::SafeAddUnsignedWrap(lhs, rhs, res);
  } else {
    if (rhs > std::numeric_limits<T>::max() - lhs) {
      return false;
    }
    *res = lhs + rhs;
    return true;
  }
}

template <typename T>
[[nodiscard]] constexpr bool SafeSub(T lhs, T rhs, T* res) {
  if constexpr (std::is_signed_v<T>) {
    return detail::SafeSubUnsignedWrap(lhs, rhs, res);
  } else {
    if (rhs > lhs) {
      return false;
    }
    *res = lhs - rhs;
    return true;
  }
}

template <typename T>
[[nodiscard]] constexpr bool SafeMul(T lhs, T rhs, T* res) {
  if constexpr (std::is_signed_v<T>) {
    return detail::SafeMulUnsignedWrap(lhs, rhs, res);
  } else {
    if (lhs != 0 && rhs > std::numeric_limits<T>::max() / lhs) {
      return false;
    }
    *res = lhs * rhs;
    return true;
  }
}

#else

template <typename T>
[[nodiscard]] constexpr bool SafeAdd(T lhs, T rhs, T* res) {
  return !__builtin_add_overflow(lhs, rhs, res);
}

template <typename T>
[[nodiscard]] constexpr bool SafeSub(T lhs, T rhs, T* res) {
  return !__builtin_sub_overflow(lhs, rhs, res);
}

template <typename T>
[[nodiscard]] constexpr bool SafeMul(T lhs, T rhs, T* res) {
  return !__builtin_mul_overflow(lhs, rhs, res);
}

#endif  // _MSC_VER

}  // namespace mozilla

#endif /* mozilla_CheckedArithmetic_h */
