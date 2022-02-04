/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Maybe.h"
#include "mozilla/TextUtils.h"
#include "mozilla/Types.h"
#include "mozilla/Utf8.h"

#include <functional>  // for std::function
#include <stddef.h>
#include <stdint.h>

MFBT_API bool mozilla::detail::IsValidUtf8(const void* aCodeUnits,
                                           size_t aCount) {
  const auto* s = reinterpret_cast<const unsigned char*>(aCodeUnits);
  const auto* const limit = s + aCount;

  while (s < limit) {
    unsigned char c = *s++;

    // If the first byte is ASCII, it's the only one in the code point.  Have a
    // fast path that avoids all the rest of the work and looping in that case.
    if (IsAscii(c)) {
      continue;
    }

    Maybe<char32_t> maybeCodePoint =
        DecodeOneUtf8CodePoint(Utf8Unit(c), &s, limit);
    if (maybeCodePoint.isNothing()) {
      return false;
    }
  }

  MOZ_ASSERT(s == limit);
  return true;
}

#if !MOZ_HAS_JSRUST()
#  include <memory>          // for std::shared_ptr
#  include "unicode/ucnv.h"  // for UConverter

mozilla::Tuple<std::shared_ptr<UConverter>, UErrorCode> _getUConverter() {
  static thread_local UErrorCode uConverterErr = U_ZERO_ERROR;
  static thread_local std::shared_ptr<UConverter> utf8Cnv(
      ucnv_open("UTF-8", &uConverterErr), ucnv_close);
  return mozilla::MakeTuple(utf8Cnv, uConverterErr);
}

mozilla::Tuple<size_t, size_t> mozilla::ConvertUtf16toUtf8Partial(
    mozilla::Span<const char16_t> aSource, mozilla::Span<char> aDest) {
  const char16_t* srcOrigPtr = aSource.Elements();
  const char16_t* srcPtr = srcOrigPtr;
  const char16_t* srcLimit = srcPtr + aSource.Length();
  char* dstOrigPtr = aDest.Elements();
  char* dstPtr = dstOrigPtr;
  const char* dstLimit = dstPtr + aDest.Length();

  // Thread-local instance of a UTF-8 converter
  std::shared_ptr<UConverter> utf8Cnv;
  UErrorCode uConverterErr;
  Tie(utf8Cnv, uConverterErr) = _getUConverter();
  UConverter* utf8Conv = utf8Cnv.get();
  if (MOZ_LIKELY(U_SUCCESS(uConverterErr) && utf8Conv != NULL)) {
    UErrorCode err = U_ZERO_ERROR;
    do {
      ucnv_fromUnicode(utf8Conv, &dstPtr, dstLimit, &srcPtr, srcLimit, nullptr,
                       true, &err);
      ucnv_reset(utf8Conv); /* ucnv_fromUnicode is a stateful operation */
      if (MOZ_UNLIKELY(U_FAILURE(err))) {
        if (err == U_BUFFER_OVERFLOW_ERROR) {
          const size_t firstInvalid =
              Utf8ValidUpToIndex(Span(dstOrigPtr, dstPtr));
          MOZ_ASSERT(static_cast<size_t>(srcPtr - srcOrigPtr) >= 0);
          MOZ_ASSERT(static_cast<size_t>(dstLimit - dstOrigPtr) >=
                     firstInvalid);
          const size_t incorrectCharLen =
              static_cast<size_t>(dstLimit - dstOrigPtr) - firstInvalid;
          char* ptr = dstOrigPtr + firstInvalid;
          switch (incorrectCharLen) {
            case 3:
              // TRIPLE_BYTE_REPLACEMENT_CHAR
              *ptr++ = 0xEF;
              *ptr++ = 0xBF;
              *ptr++ = 0xBD;
              break;
            case 2:
              // DOUBLE_BYTE_REPLACEMENT_CHAR
              *ptr++ = 0xC2;
              *ptr++ = 0xBF;
              break;
            case 1:
              // SINGLE_BYTE_REPLACEMENT_CHAR
            default:
              for (; ptr < dstLimit; ++ptr) {
                *ptr = '?';  // REPLACEMENT CHAR
              }
              break;
            case 0:
              break;
          }
          return mozilla::MakeTuple(static_cast<size_t>(srcPtr - srcOrigPtr),
                                    static_cast<size_t>(dstPtr - dstOrigPtr));
        } else {
          // We do not need to handle it, as the problematic character will be
          // replaced with a REPLACEMENT CHARACTER.
        }
      }

      if (MOZ_UNLIKELY(srcPtr < srcLimit && dstPtr < dstLimit)) {
        ++srcPtr;
        *dstPtr = '?';  // REPLACEMENT CHAR
        ++dstPtr;
      }
    } while (srcPtr < srcLimit && dstPtr < dstLimit);
  }

  return mozilla::MakeTuple(static_cast<size_t>(srcPtr - srcOrigPtr),
                            static_cast<size_t>(dstPtr - dstOrigPtr));
}

size_t mozilla::ConvertUtf16toUtf8(mozilla::Span<const char16_t> aSource,
                                   mozilla::Span<char> aDest) {
  MOZ_ASSERT(aDest.Length() >= aSource.Length() * 3);
  size_t read;
  size_t written;
  Tie(read, written) = ConvertUtf16toUtf8Partial(aSource, aDest);
  MOZ_ASSERT(read == aSource.Length());
  return written;
}

size_t mozilla::ConvertUtf8toUtf16(mozilla::Span<const char> aSource,
                                   mozilla::Span<char16_t> aDest) {
  MOZ_ASSERT(aDest.Length() > aSource.Length());

  const char* srcOrigPtr = aSource.Elements();
  const char* srcPtr = srcOrigPtr;
  const char* srcLimit = srcPtr + aSource.Length();
  char16_t* dstOrigPtr = aDest.Elements();
  char16_t* dstPtr = dstOrigPtr;
  const char16_t* dstLimit = dstPtr + aDest.Length();

  // Thread-local instance of a UTF-8 converter
  std::shared_ptr<UConverter> utf8Cnv;
  UErrorCode uConverterErr;
  Tie(utf8Cnv, uConverterErr) = _getUConverter();
  UConverter* utf8Conv = utf8Cnv.get();

  if (MOZ_LIKELY(U_SUCCESS(uConverterErr) && utf8Conv != NULL)) {
    UErrorCode err = U_ZERO_ERROR;
    do {
      ucnv_toUnicode(utf8Conv, &dstPtr, dstLimit, &srcPtr, srcLimit, nullptr,
                     true, &err);
      if (MOZ_UNLIKELY(U_FAILURE(err))) {
        // We do not need to handle it, as the problematic character will be
        // replaced with a REPLACEMENT CHARACTER.
      }

      if (MOZ_UNLIKELY(srcPtr < srcLimit && dstPtr < dstLimit)) {
        ++srcPtr;
        *dstPtr = '?';  // REPLACEMENT CHAR
        ++dstPtr;
      }
    } while (srcPtr < srcLimit && dstPtr < dstLimit);
  }
  return static_cast<size_t>(dstPtr - dstOrigPtr);
}

size_t mozilla::UnsafeConvertValidUtf8toUtf16(mozilla::Span<const char> aSource,
                                              mozilla::Span<char16_t> aDest) {
  const char* srcOrigPtr = aSource.Elements();
  const char* srcPtr = srcOrigPtr;
  size_t srcLen = aSource.Length();
  const char* srcLimit = srcPtr + srcLen;
  char16_t* dstOrigPtr = aDest.Elements();
  char16_t* dstPtr = dstOrigPtr;
  size_t dstLen = aDest.Length();
  const char16_t* dstLimit = dstPtr + dstLen;

  MOZ_ASSERT(dstLen >= srcLen);

  // Thread-local instance of a UTF-8 converter
  std::shared_ptr<UConverter> utf8Cnv;
  UErrorCode uConverterErr;
  Tie(utf8Cnv, uConverterErr) = _getUConverter();
  UConverter* utf8Conv = utf8Cnv.get();

  if (MOZ_LIKELY(U_SUCCESS(uConverterErr) && utf8Conv != NULL)) {
    UErrorCode err = U_ZERO_ERROR;

    ucnv_toUnicode(utf8Conv, &dstPtr, dstLimit, &srcPtr, srcLimit, nullptr,
                   true, &err);
    MOZ_ASSERT(!U_FAILURE(err));

    MOZ_ASSERT(srcPtr == srcLimit);
  }

  return static_cast<size_t>(dstPtr - dstOrigPtr);
}

#endif  // !MOZ_HAS_JSRUST()
