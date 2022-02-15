/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Latin1.h"
#include "mozilla/Utf8.h"

using mozilla::ArrayEqual;
using mozilla::ArrayLength;
using mozilla::ConvertLatin1toUtf16;
using mozilla::ConvertLatin1toUtf8;
using mozilla::ConvertLatin1toUtf8Partial;
using mozilla::ConvertUtf16toUtf8;
using mozilla::ConvertUtf16toUtf8Partial;
using mozilla::IsUtf16Latin1;
using mozilla::IsUtf8Latin1;
using mozilla::LossyConvertUtf16toLatin1;
using mozilla::LossyConvertUtf8toLatin1;
using mozilla::Span;
using mozilla::Tie;
using mozilla::UnsafeIsValidUtf8Latin1;
using mozilla::UnsafeValidUtf8Lati1UpTo;
using mozilla::Utf8Latin1UpTo;

static void TestIsUtf16Latin1Success() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  for (size_t i = 0; i < srcLen; ++i) {
    MOZ_RELEASE_ASSERT(IsUtf16Latin1(Span(src + i, src + srcLen)));
  }
}

static void TestIsUtf16Latin1Fail() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  for (size_t i = 0; i < srcLen; ++i) {
    Span tail(src + i, src + srcLen);

    for (size_t j = 0; j < tail.Length(); ++j) {
      tail[j] = static_cast<char16_t>(0x100 + j);
      MOZ_RELEASE_ASSERT(!IsUtf16Latin1(tail));
    }
  }
}

static void TestIsUtf8Latin1Success() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  static const size_t dstLen = srcLen * 3;
  static char dst[dstLen];
  const Span dstSpan(dst, dst + dstLen);
  for (size_t i = 0; i < srcLen; ++i) {
    memset(dst, 0, dstLen * sizeof(char));
    const Span srcTail(src + i, src + srcLen);
    const size_t written = ConvertUtf16toUtf8(srcTail, dstSpan);
    const Span subDstWithData(dst, dst + written);
    MOZ_RELEASE_ASSERT(IsUtf8Latin1(subDstWithData));
  }
}

static void TestIsUtf8Latin1Fail() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  static const size_t dstLen = srcLen * 3;
  static char dst[dstLen];
  const Span dstSpan(dst, dst + dstLen);
  for (size_t i = 0; i < srcLen; ++i) {
    memset(dst, 0, dstLen * sizeof(char));
    Span srcTail(src + i, src + srcLen);
    for (size_t j = 0; j < srcTail.Length(); ++j) {
      srcTail[j] = static_cast<char16_t>(0x100 + j);
      const size_t written = ConvertUtf16toUtf8(srcTail, dstSpan);
      const Span subDstWithData(dst, dst + written);
      MOZ_RELEASE_ASSERT(!IsUtf8Latin1(subDstWithData));
    }
  }
}

static void TestIsUtf8Latin1Invalid() {
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("\xC3"));
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("a\xC3"));
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("\xFF"));
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("a\xFF"));
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("\xC3\xFF"));
  MOZ_RELEASE_ASSERT(!IsUtf8Latin1("a\xC3\xFF"));
}

static void TestUnsafeIsValidUtf8Latin1Success() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  static const size_t dstLen = srcLen * 3;
  static char dst[dstLen];
  for (size_t i = 0; i < srcLen; ++i) {
    memset(dst, 0, dstLen * sizeof(char));
    Span srcTail(src + i, src + srcLen);
    Span dstSpan(dst + i, dst + dstLen);
    for (size_t j = 0; j < srcTail.Length(); ++j) {
      const size_t written = ConvertUtf16toUtf8(srcTail, dstSpan);
      const Span subDstWithData(dst, dst + written);
      MOZ_RELEASE_ASSERT(UnsafeIsValidUtf8Latin1(subDstWithData));
    }
  }
}

static void TestUnsafeIsValidUtf8Latin1Fail() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
  }

  static const size_t dstLen = srcLen * 3;
  static char dst[dstLen];
  const Span dstSpan(dst, dst + dstLen);
  for (size_t i = 0; i < srcLen; ++i) {
    memset(dst, 0, dstLen * sizeof(char));
    Span srcTail(src + i, src + srcLen);
    for (size_t j = 0; j < srcTail.Length(); ++j) {
      srcTail[j] = static_cast<char16_t>(0x100 + j);
      const size_t written = ConvertUtf16toUtf8(srcTail, dstSpan);
      const Span subDstWithData(dst, dst + written);
      MOZ_RELEASE_ASSERT(!UnsafeIsValidUtf8Latin1(subDstWithData));
    }
  }
}

static void TestCheckUtf8ForLatin1() {
  static const char bytes0[] = "abcdefghijklmnopaabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      UnsafeIsValidUtf8Latin1(Span(bytes0, ArrayLength(bytes0))));
  static const char bytes1[] = "abcdefghijklmnop\u00FEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      UnsafeIsValidUtf8Latin1(Span(bytes1, ArrayLength(bytes1))));
  static const char bytes2[] = "abcdefghijklmnop\u03B1abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes2, ArrayLength(bytes2))));
  static const char bytes3[] = "abcdefghijklmnop\u3041abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes3, ArrayLength(bytes3))));
  static const char bytes4[] = "abcdefghijklmnop\U0001F4A9abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes4, ArrayLength(bytes4))));
  static const char bytes5[] = "abcdefghijklmnop\uFE00abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes5, ArrayLength(bytes5))));
  static const char bytes6[] = "abcdefghijklmnop\u202Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes6, ArrayLength(bytes6))));
  static const char bytes7[] = "abcdefghijklmnop\uFEFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes7, ArrayLength(bytes7))));
  static const char bytes8[] = "abcdefghijklmnop\u0590abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes8, ArrayLength(bytes8))));
  static const char bytes9[] = "abcdefghijklmnop\u08FFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes9, ArrayLength(bytes9))));
  static const char bytes10[] = "abcdefghijklmnop\u061Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes10, ArrayLength(bytes10))));
  static const char bytes11[] = "abcdefghijklmnop\uFB50abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes11, ArrayLength(bytes11))));
  static const char bytes12[] = "abcdefghijklmnop\uFDFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes12, ArrayLength(bytes12))));
  static const char bytes13[] = "abcdefghijklmnop\uFE70abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes13, ArrayLength(bytes13))));
  static const char bytes14[] = "abcdefghijklmnop\uFEFEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes14, ArrayLength(bytes14))));
  static const char bytes15[] = "abcdefghijklmnop\u200Fabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes15, ArrayLength(bytes15))));
  static const char bytes16[] = "abcdefghijklmnop\u202Babcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes16, ArrayLength(bytes16))));
  static const char bytes17[] = "abcdefghijklmnop\u202Eabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes17, ArrayLength(bytes17))));
  static const char bytes18[] = "abcdefghijklmnop\u2067abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes18, ArrayLength(bytes18))));
  static const char bytes19[] = "abcdefghijklmnop\U00010800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes19, ArrayLength(bytes19))));
  static const char bytes20[] = "abcdefghijklmnop\u10FFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes20, ArrayLength(bytes20))));
  static const char bytes21[] = "abcdefghijklmnop\U0001E800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes21, ArrayLength(bytes21))));
  static const char bytes22[] = "abcdefghijklmnop\U0001EFFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes22, ArrayLength(bytes22))));
}

static void TestCheckStrForLatin1() {
  static const char bytes0[] = "abcdefghijklmnopaabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      UnsafeIsValidUtf8Latin1(Span(bytes0, ArrayLength(bytes0))));
  static const char bytes1[] = "abcdefghijklmnop\u00FEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      UnsafeIsValidUtf8Latin1(Span(bytes1, ArrayLength(bytes1))));
  static const char bytes2[] = "abcdefghijklmnop\u03B1abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes2, ArrayLength(bytes2))));
  static const char bytes3[] = "abcdefghijklmnop\u3041abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes3, ArrayLength(bytes3))));
  static const char bytes4[] = "abcdefghijklmnop\U0001F4A9abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes4, ArrayLength(bytes4))));
  static const char bytes5[] = "abcdefghijklmnop\uFE00abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes5, ArrayLength(bytes5))));
  static const char bytes6[] = "abcdefghijklmnop\u202Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes6, ArrayLength(bytes6))));
  static const char bytes7[] = "abcdefghijklmnop\uFEFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes7, ArrayLength(bytes7))));
  static const char bytes8[] = "abcdefghijklmnop\u0590abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes8, ArrayLength(bytes8))));
  static const char bytes9[] = "abcdefghijklmnop\u08FFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes9, ArrayLength(bytes9))));
  static const char bytes10[] = "abcdefghijklmnop\u061Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes10, ArrayLength(bytes10))));
  static const char bytes11[] = "abcdefghijklmnop\uFB50abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes11, ArrayLength(bytes11))));
  static const char bytes12[] = "abcdefghijklmnop\uFDFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes12, ArrayLength(bytes12))));
  static const char bytes13[] = "abcdefghijklmnop\uFE70abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes13, ArrayLength(bytes13))));
  static const char bytes14[] = "abcdefghijklmnop\uFEFEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes14, ArrayLength(bytes14))));
  static const char bytes15[] = "abcdefghijklmnop\u200Fabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes15, ArrayLength(bytes15))));
  static const char bytes16[] = "abcdefghijklmnop\u202Babcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes16, ArrayLength(bytes16))));
  static const char bytes17[] = "abcdefghijklmnop\u202Eabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes17, ArrayLength(bytes17))));
  static const char bytes18[] = "abcdefghijklmnop\u2067abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes18, ArrayLength(bytes18))));
  static const char bytes19[] = "abcdefghijklmnop\U00010800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes19, ArrayLength(bytes19))));
  static const char bytes20[] = "abcdefghijklmnop\u10FFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes20, ArrayLength(bytes20))));
  static const char bytes21[] = "abcdefghijklmnop\U0001E800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes21, ArrayLength(bytes21))));
  static const char bytes22[] = "abcdefghijklmnop\U0001EFFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      !UnsafeIsValidUtf8Latin1(Span(bytes22, ArrayLength(bytes22))));
}

static void TestConvertUtf16ToLatin1Lossy() {
  static const size_t srcLen = 256;
  static char16_t src[srcLen];
  static const size_t referenceLen = srcLen;
  static char reference[referenceLen];

  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char16_t>(i);
    reference[i] = static_cast<char>(i);
  }

  static const size_t dstLen = srcLen;
  static char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  LossyConvertUtf16toLatin1(Span(src, srcLen), Span(dst, dstLen));
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, dstLen));
}

static void TestConvertUtf8ToLatin1Lossy() {
  static const size_t src16Len = 256;
  static char16_t src16[src16Len];
  static const size_t referenceLen = src16Len;
  static char reference[referenceLen];

  for (size_t i = 0; i < src16Len; ++i) {
    src16[i] = static_cast<char16_t>(i);
    reference[i] = static_cast<char>(i);
  }

  static const size_t srcLen = src16Len * 3;
  static char src[srcLen];
  memset(src, 0, srcLen * sizeof(char));
  const size_t written =
      ConvertUtf16toUtf8(Span(src16, src16Len), Span(src, srcLen));
  const Span srcSpan(src, written);

  static const size_t dstLen = srcSpan.Length();
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  const size_t dstLenFilled =
      LossyConvertUtf8toLatin1(srcSpan, Span(dst, dstLen));

  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, dstLenFilled));
}

static void TestConvertLatin1ToUtf8Partial() {
  static const char src[] = "a\xFF";
  static const size_t dstLen = 2;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));

  size_t read;
  size_t written;
  Tie(read, written) = ConvertLatin1toUtf8Partial(Span(src, ArrayLength(src)),
                                                  Span(dst, dstLen));
  MOZ_RELEASE_ASSERT(read == 1);
  MOZ_RELEASE_ASSERT(written == 1);
}

static void TestConvertLatin1ToUtf8() {
  static const size_t src8Len = 256;
  static char src8[src8Len];
  static const size_t referenceLen = src8Len;
  static char16_t reference[referenceLen];

  for (size_t i = 0; i < src8Len; ++i) {
    src8[i] = static_cast<char>(i);
    reference[i] = static_cast<char16_t>(i);
  }

  static const size_t srcLen = src8Len * 3;
  static char src[srcLen];
  memset(src, 0, srcLen * sizeof(char));
  const size_t written =
      ConvertUtf16toUtf8(Span(reference, referenceLen), Span(src, srcLen));
  const Span srcSpan(src, written);

  static const size_t dstLen = srcSpan.Length() * 2;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  const size_t dstLenFilled =
      ConvertLatin1toUtf8(Span(src8, src8Len), Span(dst, dstLen));

  MOZ_RELEASE_ASSERT(ArrayEqual(dst, src, dstLenFilled));
}

static void TestConvertLatin1ToUtf16() {
  static const size_t srcLen = 256;
  static char src[srcLen];
  static const size_t referenceLen = srcLen;
  static char16_t reference[referenceLen];

  for (size_t i = 0; i < srcLen; ++i) {
    src[i] = static_cast<char>(i);
    reference[i] = static_cast<char16_t>(i);
  }

  static const size_t dstLen = srcLen;
  char16_t dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char16_t));
  ConvertLatin1toUtf16(Span(src, srcLen), Span(dst, dstLen));

  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, dstLen));
}

static void TestUtf8Latin1UpTo() {
  static const size_t baseLen = strlen("abcdefghijklmnop");
  static const char bytes0[] = "abcdefghijklmnopaabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8Latin1UpTo(Span(bytes0, ArrayLength(bytes0))) ==
                     ArrayLength(bytes0));
  static const char bytes1[] = "abcdefghijklmnop\u00FEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8Latin1UpTo(Span(bytes1, ArrayLength(bytes1))) ==
                     ArrayLength(bytes1));
  static const char bytes2[] = "abcdefghijklmnop\u03B1abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8Latin1UpTo(Span(bytes2, ArrayLength(bytes2))) ==
                     baseLen);
  static const char bytes23[] =
      "abcdefghijklmnop\x80\xBF"
      "abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8Latin1UpTo(Span(bytes23, ArrayLength(bytes23))) ==
                     baseLen);
}

static void TestUnsafeValidUtf8Lati1UpTo() {
  static const size_t baseLen = strlen("abcdefghijklmnop");
  static const char bytes0[] = "abcdefghijklmnopaabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(UnsafeValidUtf8Lati1UpTo(Span(
                         bytes0, ArrayLength(bytes0))) == ArrayLength(bytes0));
  static const char bytes1[] = "abcdefghijklmnop\u00FEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(UnsafeValidUtf8Lati1UpTo(Span(
                         bytes1, ArrayLength(bytes1))) == ArrayLength(bytes1));
  static const char bytes2[] = "abcdefghijklmnop\u03B1abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(
      UnsafeValidUtf8Lati1UpTo(Span(bytes2, ArrayLength(bytes2))) == baseLen);
}

int main() {
  TestIsUtf16Latin1Success();
  TestIsUtf16Latin1Fail();
  TestIsUtf8Latin1Success();
  TestIsUtf8Latin1Fail();
  TestIsUtf8Latin1Invalid();
  TestUnsafeIsValidUtf8Latin1Success();
  TestUnsafeIsValidUtf8Latin1Fail();
  TestCheckUtf8ForLatin1();
  TestCheckStrForLatin1();
  TestConvertUtf16ToLatin1Lossy();
  TestConvertUtf8ToLatin1Lossy();
  TestConvertLatin1ToUtf8Partial();
  TestConvertLatin1ToUtf8();
  TestConvertLatin1ToUtf16();
  TestUtf8Latin1UpTo();
  TestUnsafeValidUtf8Lati1UpTo();
}
