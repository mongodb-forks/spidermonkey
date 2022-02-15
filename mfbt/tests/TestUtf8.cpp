/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define MOZ_PRETEND_NO_JSRUST 1

#include "mozilla/Utf8.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/EnumSet.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/TextUtils.h"

using mozilla::ArrayEqual;
using mozilla::ArrayLength;
using mozilla::AsChars;
using mozilla::ConvertUtf16toUtf8;
using mozilla::ConvertUtf16toUtf8Partial;
using mozilla::ConvertUtf8toUtf16;
using mozilla::ConvertUtf8toUtf16WithoutReplacement;
using mozilla::DecodeOneUtf8CodePoint;
using mozilla::EnumSet;
using mozilla::IntegerRange;
using mozilla::IsAscii;
using mozilla::IsUtf8;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::Span;
using mozilla::Tie;
using mozilla::UnsafeConvertValidUtf8toUtf16;
using mozilla::Utf8Unit;
using mozilla::Utf8ValidUpTo;

// Disable the C++ 2a warning. See bug #1509926
#if defined(__clang__) && (__clang_major__ >= 6)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc++2a-compat"
#endif

static void TestUtf8Unit() {
  Utf8Unit c('A');
  MOZ_RELEASE_ASSERT(c.toChar() == 'A');
  MOZ_RELEASE_ASSERT(c == Utf8Unit('A'));
  MOZ_RELEASE_ASSERT(c != Utf8Unit('B'));
  MOZ_RELEASE_ASSERT(c.toUint8() == 0x41);

  unsigned char asUnsigned = 'A';
  MOZ_RELEASE_ASSERT(c.toUnsignedChar() == asUnsigned);
  MOZ_RELEASE_ASSERT(Utf8Unit('B').toUnsignedChar() != asUnsigned);

  Utf8Unit first('@');
  Utf8Unit second('#');

  MOZ_RELEASE_ASSERT(first != second);

  first = second;
  MOZ_RELEASE_ASSERT(first == second);
}

template <typename Char>
struct ToUtf8Units {
 public:
  explicit ToUtf8Units(const Char* aStart, const Char* aEnd)
      : lead(Utf8Unit(aStart[0])), iter(aStart + 1), end(aEnd) {
    MOZ_RELEASE_ASSERT(!IsAscii(aStart[0]));
  }

  const Utf8Unit lead;
  const Char* iter;
  const Char* const end;
};

class AssertIfCalled {
 public:
  template <typename... Args>
  void operator()(Args&&... aArgs) {
    MOZ_RELEASE_ASSERT(false, "AssertIfCalled instance was called");
  }
};

// NOTE: For simplicity in treating |aCharN| identically regardless whether it's
//       a string literal or a more-generalized array, we require |aCharN| be
//       null-terminated.

template <typename Char, size_t N>
static void ExpectValidCodePoint(const Char (&aCharN)[N],
                                 char32_t aExpectedCodePoint) {
  MOZ_RELEASE_ASSERT(aCharN[N - 1] == 0,
                     "array must be null-terminated for |aCharN + N - 1| to "
                     "compute the value of |aIter| as altered by "
                     "DecodeOneUtf8CodePoint");

  ToUtf8Units<Char> simpleUnit(aCharN, aCharN + N - 1);
  auto simple =
      DecodeOneUtf8CodePoint(simpleUnit.lead, &simpleUnit.iter, simpleUnit.end);
  MOZ_RELEASE_ASSERT(simple.isSome());
  MOZ_RELEASE_ASSERT(*simple == aExpectedCodePoint);
  MOZ_RELEASE_ASSERT(simpleUnit.iter == simpleUnit.end);

  ToUtf8Units<Char> complexUnit(aCharN, aCharN + N - 1);
  auto complex = DecodeOneUtf8CodePoint(
      complexUnit.lead, &complexUnit.iter, complexUnit.end, AssertIfCalled(),
      AssertIfCalled(), AssertIfCalled(), AssertIfCalled(), AssertIfCalled());
  MOZ_RELEASE_ASSERT(complex.isSome());
  MOZ_RELEASE_ASSERT(*complex == aExpectedCodePoint);
  MOZ_RELEASE_ASSERT(complexUnit.iter == complexUnit.end);
}

enum class InvalidUtf8Reason {
  BadLeadUnit,
  NotEnoughUnits,
  BadTrailingUnit,
  BadCodePoint,
  NotShortestForm,
};

template <typename Char, size_t N>
static void ExpectInvalidCodePointHelper(const Char (&aCharN)[N],
                                         InvalidUtf8Reason aExpectedReason,
                                         uint8_t aExpectedUnitsAvailable,
                                         uint8_t aExpectedUnitsNeeded,
                                         char32_t aExpectedBadCodePoint,
                                         uint8_t aExpectedUnitsObserved) {
  MOZ_RELEASE_ASSERT(aCharN[N - 1] == 0,
                     "array must be null-terminated for |aCharN + N - 1| to "
                     "compute the value of |aIter| as altered by "
                     "DecodeOneUtf8CodePoint");

  ToUtf8Units<Char> simpleUnit(aCharN, aCharN + N - 1);
  auto simple =
      DecodeOneUtf8CodePoint(simpleUnit.lead, &simpleUnit.iter, simpleUnit.end);
  MOZ_RELEASE_ASSERT(simple.isNothing());
  MOZ_RELEASE_ASSERT(static_cast<const void*>(simpleUnit.iter) == aCharN);

  EnumSet<InvalidUtf8Reason> reasons;
  uint8_t unitsAvailable;
  uint8_t unitsNeeded;
  char32_t badCodePoint;
  uint8_t unitsObserved;

  struct OnNotShortestForm {
    EnumSet<InvalidUtf8Reason>& reasons;
    char32_t& badCodePoint;
    uint8_t& unitsObserved;

    void operator()(char32_t aBadCodePoint, uint8_t aUnitsObserved) {
      reasons += InvalidUtf8Reason::NotShortestForm;
      badCodePoint = aBadCodePoint;
      unitsObserved = aUnitsObserved;
    }
  };

  ToUtf8Units<Char> complexUnit(aCharN, aCharN + N - 1);
  auto complex = DecodeOneUtf8CodePoint(
      complexUnit.lead, &complexUnit.iter, complexUnit.end,
      [&reasons]() { reasons += InvalidUtf8Reason::BadLeadUnit; },
      [&reasons, &unitsAvailable, &unitsNeeded](uint8_t aUnitsAvailable,
                                                uint8_t aUnitsNeeded) {
        reasons += InvalidUtf8Reason::NotEnoughUnits;
        unitsAvailable = aUnitsAvailable;
        unitsNeeded = aUnitsNeeded;
      },
      [&reasons, &unitsObserved](uint8_t aUnitsObserved) {
        reasons += InvalidUtf8Reason::BadTrailingUnit;
        unitsObserved = aUnitsObserved;
      },
      [&reasons, &badCodePoint, &unitsObserved](char32_t aBadCodePoint,
                                                uint8_t aUnitsObserved) {
        reasons += InvalidUtf8Reason::BadCodePoint;
        badCodePoint = aBadCodePoint;
        unitsObserved = aUnitsObserved;
      },
      [&reasons, &badCodePoint, &unitsObserved](char32_t aBadCodePoint,
                                                uint8_t aUnitsObserved) {
        reasons += InvalidUtf8Reason::NotShortestForm;
        badCodePoint = aBadCodePoint;
        unitsObserved = aUnitsObserved;
      });
  MOZ_RELEASE_ASSERT(complex.isNothing());
  MOZ_RELEASE_ASSERT(static_cast<const void*>(complexUnit.iter) == aCharN);

  bool alreadyIterated = false;
  for (InvalidUtf8Reason reason : reasons) {
    MOZ_RELEASE_ASSERT(!alreadyIterated);
    alreadyIterated = true;

    switch (reason) {
      case InvalidUtf8Reason::BadLeadUnit:
        break;

      case InvalidUtf8Reason::NotEnoughUnits:
        MOZ_RELEASE_ASSERT(unitsAvailable == aExpectedUnitsAvailable);
        MOZ_RELEASE_ASSERT(unitsNeeded == aExpectedUnitsNeeded);
        break;

      case InvalidUtf8Reason::BadTrailingUnit:
        MOZ_RELEASE_ASSERT(unitsObserved == aExpectedUnitsObserved);
        break;

      case InvalidUtf8Reason::BadCodePoint:
        MOZ_RELEASE_ASSERT(badCodePoint == aExpectedBadCodePoint);
        MOZ_RELEASE_ASSERT(unitsObserved == aExpectedUnitsObserved);
        break;

      case InvalidUtf8Reason::NotShortestForm:
        MOZ_RELEASE_ASSERT(badCodePoint == aExpectedBadCodePoint);
        MOZ_RELEASE_ASSERT(unitsObserved == aExpectedUnitsObserved);
        break;
    }
  }
}

// NOTE: For simplicity in treating |aCharN| identically regardless whether it's
//       a string literal or a more-generalized array, we require |aCharN| be
//       null-terminated in all these functions.

template <typename Char, size_t N>
static void ExpectBadLeadUnit(const Char (&aCharN)[N]) {
  ExpectInvalidCodePointHelper(aCharN, InvalidUtf8Reason::BadLeadUnit, 0xFF,
                               0xFF, 0xFFFFFFFF, 0xFF);
}

template <typename Char, size_t N>
static void ExpectNotEnoughUnits(const Char (&aCharN)[N],
                                 uint8_t aExpectedUnitsAvailable,
                                 uint8_t aExpectedUnitsNeeded) {
  ExpectInvalidCodePointHelper(aCharN, InvalidUtf8Reason::NotEnoughUnits,
                               aExpectedUnitsAvailable, aExpectedUnitsNeeded,
                               0xFFFFFFFF, 0xFF);
}

template <typename Char, size_t N>
static void ExpectBadTrailingUnit(const Char (&aCharN)[N],
                                  uint8_t aExpectedUnitsObserved) {
  ExpectInvalidCodePointHelper(aCharN, InvalidUtf8Reason::BadTrailingUnit, 0xFF,
                               0xFF, 0xFFFFFFFF, aExpectedUnitsObserved);
}

template <typename Char, size_t N>
static void ExpectNotShortestForm(const Char (&aCharN)[N],
                                  char32_t aExpectedBadCodePoint,
                                  uint8_t aExpectedUnitsObserved) {
  ExpectInvalidCodePointHelper(aCharN, InvalidUtf8Reason::NotShortestForm, 0xFF,
                               0xFF, aExpectedBadCodePoint,
                               aExpectedUnitsObserved);
}

template <typename Char, size_t N>
static void ExpectBadCodePoint(const Char (&aCharN)[N],
                               char32_t aExpectedBadCodePoint,
                               uint8_t aExpectedUnitsObserved) {
  ExpectInvalidCodePointHelper(aCharN, InvalidUtf8Reason::BadCodePoint, 0xFF,
                               0xFF, aExpectedBadCodePoint,
                               aExpectedUnitsObserved);
}

static void TestIsUtf8() {
  // Note we include the U+0000 NULL in this one -- and that's fine.
  static const char asciiBytes[] = u8"How about a nice game of chess?";
  MOZ_RELEASE_ASSERT(IsUtf8(Span(asciiBytes, ArrayLength(asciiBytes))));

  static const char endNonAsciiBytes[] = u8"Life is like a 🌯";
  MOZ_RELEASE_ASSERT(
      IsUtf8(Span(endNonAsciiBytes, ArrayLength(endNonAsciiBytes) - 1)));

  static const unsigned char badLeading[] = {0x80};
  MOZ_RELEASE_ASSERT(
      !IsUtf8(AsChars(Span(badLeading, ArrayLength(badLeading)))));

  // Byte-counts

  // 1
  static const char oneBytes[] = u8"A";  // U+0041 LATIN CAPITAL LETTER A
  constexpr size_t oneBytesLen = ArrayLength(oneBytes);
  static_assert(oneBytesLen == 2, "U+0041 plus nul");
  MOZ_RELEASE_ASSERT(IsUtf8(Span(oneBytes, oneBytesLen)));

  // 2
  static const char twoBytes[] = u8"؆";  // U+0606 ARABIC-INDIC CUBE ROOT
  constexpr size_t twoBytesLen = ArrayLength(twoBytes);
  static_assert(twoBytesLen == 3, "U+0606 in two bytes plus nul");
  MOZ_RELEASE_ASSERT(IsUtf8(Span(twoBytes, twoBytesLen)));

  ExpectValidCodePoint(twoBytes, 0x0606);

  // 3
  static const char threeBytes[] = u8"᨞";  // U+1A1E BUGINESE PALLAWA
  constexpr size_t threeBytesLen = ArrayLength(threeBytes);
  static_assert(threeBytesLen == 4, "U+1A1E in three bytes plus nul");
  MOZ_RELEASE_ASSERT(IsUtf8(Span(threeBytes, threeBytesLen)));

  ExpectValidCodePoint(threeBytes, 0x1A1E);

  // 4
  static const char fourBytes[] =
      u8"🁡";  // U+1F061 DOMINO TILE HORIZONTAL-06-06
  constexpr size_t fourBytesLen = ArrayLength(fourBytes);
  static_assert(fourBytesLen == 5, "U+1F061 in four bytes plus nul");
  MOZ_RELEASE_ASSERT(IsUtf8(Span(fourBytes, fourBytesLen)));

  ExpectValidCodePoint(fourBytes, 0x1F061);

  // Max code point
  static const char maxCodePoint[] = u8"􏿿";  // U+10FFFF
  constexpr size_t maxCodePointLen = ArrayLength(maxCodePoint);
  static_assert(maxCodePointLen == 5, "U+10FFFF in four bytes plus nul");
  MOZ_RELEASE_ASSERT(IsUtf8(Span(maxCodePoint, maxCodePointLen)));

  ExpectValidCodePoint(maxCodePoint, 0x10FFFF);

  // One past max code point
  static const unsigned char onePastMaxCodePoint[] = {0xF4, 0x90, 0x80, 0x80,
                                                      0x0};
  constexpr size_t onePastMaxCodePointLen = ArrayLength(onePastMaxCodePoint);
  MOZ_RELEASE_ASSERT(
      !IsUtf8(AsChars(Span(onePastMaxCodePoint, onePastMaxCodePointLen))));

  ExpectBadCodePoint(onePastMaxCodePoint, 0x110000, 4);

  // Surrogate-related testing

  // (Note that the various code unit sequences here are null-terminated to
  // simplify life for ExpectValidCodePoint, which presumes null termination.)

  static const unsigned char justBeforeSurrogates[] = {0xED, 0x9F, 0xBF, 0x0};
  constexpr size_t justBeforeSurrogatesLen =
      ArrayLength(justBeforeSurrogates) - 1;
  MOZ_RELEASE_ASSERT(
      IsUtf8(AsChars(Span(justBeforeSurrogates, justBeforeSurrogatesLen))));

  ExpectValidCodePoint(justBeforeSurrogates, 0xD7FF);

  static const unsigned char leastSurrogate[] = {0xED, 0xA0, 0x80, 0x0};
  constexpr size_t leastSurrogateLen = ArrayLength(leastSurrogate) - 1;
  MOZ_RELEASE_ASSERT(!IsUtf8(AsChars(Span(leastSurrogate, leastSurrogateLen))));

  ExpectBadCodePoint(leastSurrogate, 0xD800, 3);

  static const unsigned char arbitraryHighSurrogate[] = {0xED, 0xA2, 0x87, 0x0};
  constexpr size_t arbitraryHighSurrogateLen =
      ArrayLength(arbitraryHighSurrogate) - 1;
  MOZ_RELEASE_ASSERT(!IsUtf8(
      AsChars(Span(arbitraryHighSurrogate, arbitraryHighSurrogateLen))));

  ExpectBadCodePoint(arbitraryHighSurrogate, 0xD887, 3);

  static const unsigned char arbitraryLowSurrogate[] = {0xED, 0xB7, 0xAF, 0x0};
  constexpr size_t arbitraryLowSurrogateLen =
      ArrayLength(arbitraryLowSurrogate) - 1;
  MOZ_RELEASE_ASSERT(
      !IsUtf8(AsChars(Span(arbitraryLowSurrogate, arbitraryLowSurrogateLen))));

  ExpectBadCodePoint(arbitraryLowSurrogate, 0xDDEF, 3);

  static const unsigned char greatestSurrogate[] = {0xED, 0xBF, 0xBF, 0x0};
  constexpr size_t greatestSurrogateLen = ArrayLength(greatestSurrogate) - 1;
  MOZ_RELEASE_ASSERT(
      !IsUtf8(AsChars(Span(greatestSurrogate, greatestSurrogateLen))));

  ExpectBadCodePoint(greatestSurrogate, 0xDFFF, 3);

  static const unsigned char justAfterSurrogates[] = {0xEE, 0x80, 0x80, 0x0};
  constexpr size_t justAfterSurrogatesLen =
      ArrayLength(justAfterSurrogates) - 1;
  MOZ_RELEASE_ASSERT(
      IsUtf8(AsChars(Span(justAfterSurrogates, justAfterSurrogatesLen))));

  ExpectValidCodePoint(justAfterSurrogates, 0xE000);
}

static void TestDecodeOneValidUtf8CodePoint() {
  // NOTE: DecodeOneUtf8CodePoint decodes only *non*-ASCII code points that
  //       consist of multiple code units, so there are no ASCII tests below.

  // Length two.

  ExpectValidCodePoint(u8"", 0x80);  // <control>
  ExpectValidCodePoint(u8"©", 0xA9);   // COPYRIGHT SIGN
  ExpectValidCodePoint(u8"¶", 0xB6);   // PILCROW SIGN
  ExpectValidCodePoint(u8"¾", 0xBE);   // VULGAR FRACTION THREE QUARTERS
  ExpectValidCodePoint(u8"÷", 0xF7);   // DIVISION SIGN
  ExpectValidCodePoint(u8"ÿ", 0xFF);   // LATIN SMALL LETTER Y WITH DIAERESIS
  ExpectValidCodePoint(u8"Ā", 0x100);  // LATIN CAPITAL LETTER A WITH MACRON
  ExpectValidCodePoint(u8"Ĳ", 0x132);  // LATIN CAPITAL LETTER LIGATURE IJ
  ExpectValidCodePoint(u8"ͼ", 0x37C);  // GREEK SMALL DOTTED LUNATE SIGMA SYMBOL
  ExpectValidCodePoint(u8"Ӝ",
                       0x4DC);  // CYRILLIC CAPITAL LETTER ZHE WITTH DIAERESIS
  ExpectValidCodePoint(u8"۩", 0x6E9);   // ARABIC PLACE OF SAJDAH
  ExpectValidCodePoint(u8"߿", 0x7FF);  // <not assigned>

  // Length three.

  ExpectValidCodePoint(u8"ࠀ", 0x800);    // SAMARITAN LETTER ALAF
  ExpectValidCodePoint(u8"ࡁ", 0x841);    // MANDAIC LETTER AB
  ExpectValidCodePoint(u8"ࣿ", 0x8FF);  // ARABIC MARK SIDEWAYS NOON GHUNNA
  ExpectValidCodePoint(u8"ஆ", 0xB86);    // TAMIL LETTER AA
  ExpectValidCodePoint(u8"༃",
                       0xF03);  // TIBETAN MARK GTER YIG MGO -UM GTER TSHEG MA
  ExpectValidCodePoint(
      u8"࿉",
      0xFC9);  // TIBETAN SYMBOL NOR BU (but on my system it really looks like
               // SOFT-SERVE ICE CREAM FROM ABOVE THE PLANE if you ask me)
  ExpectValidCodePoint(u8"ဪ", 0x102A);           // MYANMAR LETTER AU
  ExpectValidCodePoint(u8"ᚏ", 0x168F);           // OGHAM LETTER RUIS
  ExpectValidCodePoint("\xE2\x80\xA8", 0x2028);  // (the hated) LINE SEPARATOR
  ExpectValidCodePoint("\xE2\x80\xA9",
                       0x2029);           // (the hated) PARAGRAPH SEPARATOR
  ExpectValidCodePoint(u8"☬", 0x262C);    // ADI SHAKTI
  ExpectValidCodePoint(u8"㊮", 0x32AE);   // CIRCLED IDEOGRAPH RESOURCE
  ExpectValidCodePoint(u8"㏖", 0x33D6);   // SQUARE MOL
  ExpectValidCodePoint(u8"ꔄ", 0xA504);    // VAI SYLLABLE WEEN
  ExpectValidCodePoint(u8"ퟕ", 0xD7D5);   // HANGUL JONGSEONG RIEUL-SSANGKIYEOK
  ExpectValidCodePoint(u8"퟿", 0xD7FF);  // <not assigned>
  ExpectValidCodePoint(u8"", 0xE000);    // <Private Use>
  ExpectValidCodePoint(u8"鱗", 0xF9F2);   // CJK COMPATIBILITY IDEOGRAPH-F9F
  ExpectValidCodePoint(
      u8"﷽", 0xFDFD);  // ARABIC LIGATURE BISMILLAH AR-RAHMAN AR-RAHHHEEEEM
  ExpectValidCodePoint(u8"￿", 0xFFFF);  // <not assigned>

  // Length four.
  ExpectValidCodePoint(u8"𐀀", 0x10000);      // LINEAR B SYLLABLE B008 A
  ExpectValidCodePoint(u8"𔑀", 0x14440);   // ANATOLIAN HIEROGLYPH A058
  ExpectValidCodePoint(u8"𝛗", 0x1D6D7);      // MATHEMATICAL BOLD SMALL PHI
  ExpectValidCodePoint(u8"💩", 0x1F4A9);      // PILE OF POO
  ExpectValidCodePoint(u8"🔫", 0x1F52B);      // PISTOL
  ExpectValidCodePoint(u8"🥌", 0x1F94C);   // CURLING STONE
  ExpectValidCodePoint(u8"🥏", 0x1F94F);   // FLYING DISC
  ExpectValidCodePoint(u8"𠍆", 0x20346);     // CJK UNIFIED IDEOGRAPH-20346
  ExpectValidCodePoint(u8"𡠺", 0x2183A);     // CJK UNIFIED IDEOGRAPH-2183A
  ExpectValidCodePoint(u8"񁟶", 0x417F6);   // <not assigned>
  ExpectValidCodePoint(u8"񾠶", 0x7E836);   // <not assigned>
  ExpectValidCodePoint(u8"󾽧", 0xFEF67);      // <Plane 15 Private Use>
  ExpectValidCodePoint(u8"􏿿", 0x10FFFF);  //
}

static void TestDecodeBadLeadUnit() {
  // These tests are actually exhaustive.

  unsigned char badLead[] = {'\0', '\0'};

  for (uint8_t lead : IntegerRange(0b1000'0000, 0b1100'0000)) {
    badLead[0] = lead;
    ExpectBadLeadUnit(badLead);
  }

  {
    uint8_t lead = 0b1111'1000;
    do {
      badLead[0] = lead;
      ExpectBadLeadUnit(badLead);
      if (lead == 0b1111'1111) {
        break;
      }

      lead++;
    } while (true);
  }
}

static void TestTooFewOrBadTrailingUnits() {
  // Lead unit indicates a two-byte code point.

  char truncatedTwo[] = {'\0', '\0'};
  char badTrailTwo[] = {'\0', '\0', '\0'};

  for (uint8_t lead : IntegerRange(0b1100'0000, 0b1110'0000)) {
    truncatedTwo[0] = lead;
    ExpectNotEnoughUnits(truncatedTwo, 1, 2);

    badTrailTwo[0] = lead;
    for (uint8_t trail : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailTwo[1] = trail;
      ExpectBadTrailingUnit(badTrailTwo, 2);
    }

    for (uint8_t trail : IntegerRange(0b1100'0000, 0b1111'1111)) {
      badTrailTwo[1] = trail;
      ExpectBadTrailingUnit(badTrailTwo, 2);
    }
  }

  // Lead unit indicates a three-byte code point.

  char truncatedThreeOne[] = {'\0', '\0'};
  char truncatedThreeTwo[] = {'\0', '\0', '\0'};
  unsigned char badTrailThree[] = {'\0', '\0', '\0', '\0'};

  for (uint8_t lead : IntegerRange(0b1110'0000, 0b1111'0000)) {
    truncatedThreeOne[0] = lead;
    ExpectNotEnoughUnits(truncatedThreeOne, 1, 3);

    truncatedThreeTwo[0] = lead;
    ExpectNotEnoughUnits(truncatedThreeTwo, 2, 3);

    badTrailThree[0] = lead;
    badTrailThree[2] = 0b1011'1111;  // make valid to test overreads
    for (uint8_t mid : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailThree[1] = mid;
      ExpectBadTrailingUnit(badTrailThree, 2);
    }
    {
      uint8_t mid = 0b1100'0000;
      do {
        badTrailThree[1] = mid;
        ExpectBadTrailingUnit(badTrailThree, 2);
        if (mid == 0b1111'1111) {
          break;
        }

        mid++;
      } while (true);
    }

    badTrailThree[1] = 0b1011'1111;
    for (uint8_t last : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailThree[2] = last;
      ExpectBadTrailingUnit(badTrailThree, 3);
    }
    {
      uint8_t last = 0b1100'0000;
      do {
        badTrailThree[2] = last;
        ExpectBadTrailingUnit(badTrailThree, 3);
        if (last == 0b1111'1111) {
          break;
        }

        last++;
      } while (true);
    }
  }

  // Lead unit indicates a four-byte code point.

  char truncatedFourOne[] = {'\0', '\0'};
  char truncatedFourTwo[] = {'\0', '\0', '\0'};
  char truncatedFourThree[] = {'\0', '\0', '\0', '\0'};

  unsigned char badTrailFour[] = {'\0', '\0', '\0', '\0', '\0'};

  for (uint8_t lead : IntegerRange(0b1111'0000, 0b1111'1000)) {
    truncatedFourOne[0] = lead;
    ExpectNotEnoughUnits(truncatedFourOne, 1, 4);

    truncatedFourTwo[0] = lead;
    ExpectNotEnoughUnits(truncatedFourTwo, 2, 4);

    truncatedFourThree[0] = lead;
    ExpectNotEnoughUnits(truncatedFourThree, 3, 4);

    badTrailFour[0] = lead;
    badTrailFour[2] = badTrailFour[3] = 0b1011'1111;  // test for overreads
    for (uint8_t second : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailFour[1] = second;
      ExpectBadTrailingUnit(badTrailFour, 2);
    }
    {
      uint8_t second = 0b1100'0000;
      do {
        badTrailFour[1] = second;
        ExpectBadTrailingUnit(badTrailFour, 2);
        if (second == 0b1111'1111) {
          break;
        }

        second++;
      } while (true);
    }

    badTrailFour[1] = badTrailFour[3] = 0b1011'1111;  // test for overreads
    for (uint8_t third : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailFour[2] = third;
      ExpectBadTrailingUnit(badTrailFour, 3);
    }
    {
      uint8_t third = 0b1100'0000;
      do {
        badTrailFour[2] = third;
        ExpectBadTrailingUnit(badTrailFour, 3);
        if (third == 0b1111'1111) {
          break;
        }

        third++;
      } while (true);
    }

    badTrailFour[2] = 0b1011'1111;
    for (uint8_t fourth : IntegerRange(0b0000'0000, 0b1000'0000)) {
      badTrailFour[3] = fourth;
      ExpectBadTrailingUnit(badTrailFour, 4);
    }
    {
      uint8_t fourth = 0b1100'0000;
      do {
        badTrailFour[3] = fourth;
        ExpectBadTrailingUnit(badTrailFour, 4);
        if (fourth == 0b1111'1111) {
          break;
        }

        fourth++;
      } while (true);
    }
  }
}

static void TestBadSurrogate() {
  // These tests are actually exhaustive.

  ExpectValidCodePoint("\xED\x9F\xBF", 0xD7FF);  // last before surrogates
  ExpectValidCodePoint("\xEE\x80\x80", 0xE000);  // first after surrogates

  // First invalid surrogate encoding is { 0xED, 0xA0, 0x80 }.  Last invalid
  // surrogate encoding is { 0xED, 0xBF, 0xBF }.

  char badSurrogate[] = {'\xED', '\0', '\0', '\0'};

  for (char32_t c = 0xD800; c < 0xE000; c++) {
    badSurrogate[1] = 0b1000'0000 ^ ((c & 0b1111'1100'0000) >> 6);
    badSurrogate[2] = 0b1000'0000 ^ ((c & 0b0000'0011'1111));

    ExpectBadCodePoint(badSurrogate, c, 3);
  }
}

static void TestBadTooBig() {
  // These tests are actually exhaustive.

  ExpectValidCodePoint("\xF4\x8F\xBF\xBF", 0x10'FFFF);  // last code point

  // Four-byte code points are
  //
  //   0b1111'0xxx 0b10xx'xxxx 0b10xx'xxxx 0b10xx'xxxx
  //
  // with 3 + 6 + 6 + 6 == 21 unconstrained bytes, so the structurally
  // representable limit (exclusive) is 2**21 - 1 == 2097152.

  char tooLargeCodePoint[] = {'\0', '\0', '\0', '\0', '\0'};

  for (char32_t c = 0x11'0000; c < (1 << 21); c++) {
    tooLargeCodePoint[0] =
        0b1111'0000 ^ ((c & 0b1'1100'0000'0000'0000'0000) >> 18);
    tooLargeCodePoint[1] =
        0b1000'0000 ^ ((c & 0b0'0011'1111'0000'0000'0000) >> 12);
    tooLargeCodePoint[2] =
        0b1000'0000 ^ ((c & 0b0'0000'0000'1111'1100'0000) >> 6);
    tooLargeCodePoint[3] = 0b1000'0000 ^ ((c & 0b0'0000'0000'0000'0011'1111));

    ExpectBadCodePoint(tooLargeCodePoint, c, 4);
  }
}

static void TestBadCodePoint() {
  TestBadSurrogate();
  TestBadTooBig();
}

static void TestNotShortestForm() {
  {
    // One-byte in two-byte.

    char oneInTwo[] = {'\0', '\0', '\0'};

    for (char32_t c = '\0'; c < 0x80; c++) {
      oneInTwo[0] = 0b1100'0000 ^ ((c & 0b0111'1100'0000) >> 6);
      oneInTwo[1] = 0b1000'0000 ^ ((c & 0b0000'0011'1111));

      ExpectNotShortestForm(oneInTwo, c, 2);
    }

    // One-byte in three-byte.

    char oneInThree[] = {'\0', '\0', '\0', '\0'};

    for (char32_t c = '\0'; c < 0x80; c++) {
      oneInThree[0] = 0b1110'0000 ^ ((c & 0b1111'0000'0000'0000) >> 12);
      oneInThree[1] = 0b1000'0000 ^ ((c & 0b0000'1111'1100'0000) >> 6);
      oneInThree[2] = 0b1000'0000 ^ ((c & 0b0000'0000'0011'1111));

      ExpectNotShortestForm(oneInThree, c, 3);
    }

    // One-byte in four-byte.

    char oneInFour[] = {'\0', '\0', '\0', '\0', '\0'};

    for (char32_t c = '\0'; c < 0x80; c++) {
      oneInFour[0] = 0b1111'0000 ^ ((c & 0b1'1100'0000'0000'0000'0000) >> 18);
      oneInFour[1] = 0b1000'0000 ^ ((c & 0b0'0011'1111'0000'0000'0000) >> 12);
      oneInFour[2] = 0b1000'0000 ^ ((c & 0b0'0000'0000'1111'1100'0000) >> 6);
      oneInFour[3] = 0b1000'0000 ^ ((c & 0b0'0000'0000'0000'0011'1111));

      ExpectNotShortestForm(oneInFour, c, 4);
    }
  }

  {
    // Two-byte in three-byte.

    char twoInThree[] = {'\0', '\0', '\0', '\0'};

    for (char32_t c = 0x80; c < 0x800; c++) {
      twoInThree[0] = 0b1110'0000 ^ ((c & 0b1111'0000'0000'0000) >> 12);
      twoInThree[1] = 0b1000'0000 ^ ((c & 0b0000'1111'1100'0000) >> 6);
      twoInThree[2] = 0b1000'0000 ^ ((c & 0b0000'0000'0011'1111));

      ExpectNotShortestForm(twoInThree, c, 3);
    }

    // Two-byte in four-byte.

    char twoInFour[] = {'\0', '\0', '\0', '\0', '\0'};

    for (char32_t c = 0x80; c < 0x800; c++) {
      twoInFour[0] = 0b1111'0000 ^ ((c & 0b1'1100'0000'0000'0000'0000) >> 18);
      twoInFour[1] = 0b1000'0000 ^ ((c & 0b0'0011'1111'0000'0000'0000) >> 12);
      twoInFour[2] = 0b1000'0000 ^ ((c & 0b0'0000'0000'1111'1100'0000) >> 6);
      twoInFour[3] = 0b1000'0000 ^ ((c & 0b0'0000'0000'0000'0011'1111));

      ExpectNotShortestForm(twoInFour, c, 4);
    }
  }

  {
    // Three-byte in four-byte.

    char threeInFour[] = {'\0', '\0', '\0', '\0', '\0'};

    for (char32_t c = 0x800; c < 0x1'0000; c++) {
      threeInFour[0] = 0b1111'0000 ^ ((c & 0b1'1100'0000'0000'0000'0000) >> 18);
      threeInFour[1] = 0b1000'0000 ^ ((c & 0b0'0011'1111'0000'0000'0000) >> 12);
      threeInFour[2] = 0b1000'0000 ^ ((c & 0b0'0000'0000'1111'1100'0000) >> 6);
      threeInFour[3] = 0b1000'0000 ^ ((c & 0b0'0000'0000'0000'0011'1111));

      ExpectNotShortestForm(threeInFour, c, 4);
    }
  }
}

static void TestDecodeOneInvalidUtf8CodePoint() {
  TestDecodeBadLeadUnit();
  TestTooFewOrBadTrailingUnits();
  TestBadCodePoint();
  TestNotShortestForm();
}

static void TestDecodeOneUtf8CodePoint() {
  TestDecodeOneValidUtf8CodePoint();
  TestDecodeOneInvalidUtf8CodePoint();
}

static void TestUtf8ValidUpTo() {
  static const size_t baseLen = strlen("abcdefghijklmnop");
  static const char bytes0[] = "abcdefghijklmnopaabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes0, ArrayLength(bytes0))) ==
                     ArrayLength(bytes0));
  static const char bytes1[] = "abcdefghijklmnop\u00FEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes1, ArrayLength(bytes1))) ==
                     ArrayLength(bytes1));
  static const char bytes2[] = "abcdefghijklmnop\u03B1abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes2, ArrayLength(bytes2))) ==
                     ArrayLength(bytes2));
  static const char bytes3[] = "abcdefghijklmnop\u3041abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes3, ArrayLength(bytes3))) ==
                     ArrayLength(bytes3));
  static const char bytes4[] = "abcdefghijklmnop\U0001F4A9abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes4, ArrayLength(bytes4))) ==
                     ArrayLength(bytes4));
  static const char bytes5[] = "abcdefghijklmnop\uFE00abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes5, ArrayLength(bytes5))) ==
                     ArrayLength(bytes5));
  static const char bytes6[] = "abcdefghijklmnop\u202Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes6, ArrayLength(bytes6))) ==
                     ArrayLength(bytes6));
  static const char bytes7[] = "abcdefghijklmnop\uFEFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes7, ArrayLength(bytes7))) ==
                     ArrayLength(bytes7));
  static const char bytes8[] = "abcdefghijklmnop\u0590abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes8, ArrayLength(bytes8))) ==
                     ArrayLength(bytes8));
  static const char bytes9[] = "abcdefghijklmnop\u08FFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes9, ArrayLength(bytes9))) ==
                     ArrayLength(bytes9));
  static const char bytes10[] = "abcdefghijklmnop\u061Cabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes10, ArrayLength(bytes10))) ==
                     ArrayLength(bytes10));
  static const char bytes11[] = "abcdefghijklmnop\uFB50abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes11, ArrayLength(bytes11))) ==
                     ArrayLength(bytes11));
  static const char bytes12[] = "abcdefghijklmnop\uFDFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes12, ArrayLength(bytes12))) ==
                     ArrayLength(bytes12));
  static const char bytes13[] = "abcdefghijklmnop\uFE70abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes13, ArrayLength(bytes13))) ==
                     ArrayLength(bytes13));
  static const char bytes14[] = "abcdefghijklmnop\uFEFEabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes14, ArrayLength(bytes14))) ==
                     ArrayLength(bytes14));
  static const char bytes15[] = "abcdefghijklmnop\u200Fabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes15, ArrayLength(bytes15))) ==
                     ArrayLength(bytes15));
  static const char bytes16[] = "abcdefghijklmnop\u202Babcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes16, ArrayLength(bytes16))) ==
                     ArrayLength(bytes16));
  static const char bytes17[] = "abcdefghijklmnop\u202Eabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes17, ArrayLength(bytes17))) ==
                     ArrayLength(bytes17));
  static const char bytes18[] = "abcdefghijklmnop\u2067abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes18, ArrayLength(bytes18))) ==
                     ArrayLength(bytes18));
  static const char bytes19[] = "abcdefghijklmnop\U00010800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes19, ArrayLength(bytes19))) ==
                     ArrayLength(bytes19));
  static const char bytes20[] = "abcdefghijklmnop\u10FFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes20, ArrayLength(bytes20))) ==
                     ArrayLength(bytes20));
  static const char bytes21[] = "abcdefghijklmnop\U0001E800abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes21, ArrayLength(bytes21))) ==
                     ArrayLength(bytes21));
  static const char bytes22[] = "abcdefghijklmnop\U0001EFFFabcdefghijklmnop";
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes22, ArrayLength(bytes22))) ==
                     ArrayLength(bytes22));
  static const char bytes23[] =
      "abcdefghijklmnop\x80\xBF"
      "abcdefghijklmnop";
  MOZ_RELEASE_ASSERT(AsciiValidUpTo(Span(bytes23, ArrayLength(bytes23))) ==
                     baseLen);
}

static void TestConvertUtf16toUtf8Partial() {
  static const char reference[] =
      "abcdefghijklmnopqrstu\U0001F4A9v\u2603w\u00B6xyzz";
  static const size_t referenceLen = ArrayLength(reference);
  static const size_t srcLen = referenceLen + 1;
  static char16_t src[srcLen];
  size_t written =
      ConvertUtf8toUtf16(Span(reference, referenceLen), Span(src, srcLen));
  const Span srcSpan(src, written);
  const size_t dstLen = srcSpan.Length() * 3 + 1;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  size_t read;
  Tie(read, written) = ConvertUtf16toUtf8Partial(srcSpan, Span(dst, 24));
  written = ConvertUtf16toUtf8(Span(src + read, src + srcSpan.Length()),
                               Span(dst + written, dst + dstLen));
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, written));
}

static void TestConvertUtf16toUtf8() {
  static const char reference[] =
      "abcdefghijklmnopqrstu\U0001F4A9v\u2603w\u00B6xyzz";
  static const size_t referenceLen = ArrayLength(reference);
  static const size_t srcLen = referenceLen + 1;
  static char16_t src[srcLen];
  size_t written =
      ConvertUtf8toUtf16(Span(reference, referenceLen), Span(src, srcLen));
  const Span srcSpan(src, written);
  const size_t dstLen = srcSpan.Length() * 3 + 1;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  written = ConvertUtf16toUtf8(srcSpan, Span(dst, dstLen));
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, written));
}

static void TestConvertUtf8toUtf16() {
  static const char src[] = "abcdefghijklmnopqrstu\U0001F4A9v\u2603w\u00B6xyzz";
  static const size_t srcLen = ArrayLength(src);
  const char* srcPtr = src;
  const char* srcLimit = srcPtr + srcLen;
  static const size_t dstLen = srcLen + 1;
  static char16_t dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  size_t written = ConvertUtf8toUtf16(Span(src, srcLen), Span(dst, dstLen));

  static const size_t referenceLen = srcLen + 1;
  static char16_t reference[referenceLen];
  char16_t* referencePtr = reference;
  char16_t* referenceLimit = referencePtr + referenceLen;
  UErrorCode uConverterErr = U_ZERO_ERROR;
  std::shared_ptr<UConverter> utf8Cnv(ucnv_open("UTF-8", &uConverterErr),
                                      ucnv_close);
  UConverter* utf8Conv = utf8Cnv.get();
  UErrorCode err = U_ZERO_ERROR;
  ucnv_toUnicode(utf8Conv, &referencePtr, referenceLimit, &srcPtr, srcLimit,
                 nullptr, true, &err);
  MOZ_RELEASE_ASSERT(!U_FAILURE(err));
  MOZ_RELEASE_ASSERT(srcPtr == srcLimit);
  MOZ_RELEASE_ASSERT(referencePtr <= referenceLimit);

  MOZ_RELEASE_ASSERT(static_cast<size_t>(referencePtr - reference) == written);
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, reference, written));
}

static void TestConvertUtf8toUtf16WithoutReplacement() {
  static const size_t bufLen = 5;
  static char16_t buf[bufLen];
  memset(buf, 0, bufLen * sizeof(char16_t));
  Maybe<size_t> written;

  static const char src1[] = "ab";
  static const size_t src1Len =
      ArrayLength(src1) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src1, src1Len), Span(buf, 2));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 2);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>('a'));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>('b'));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src2[] =
      "\xC3\xA4"
      "c";
  static const size_t src2Len =
      ArrayLength(src2) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src2, src2Len), Span(buf, 3));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 2);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0xE4));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>('c'));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src3[] = "\xE2\x98\x83";
  static const size_t src3Len =
      ArrayLength(src3) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src3, src3Len), Span(buf, 3));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 1);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0x2603));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>('c'));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src4[] =
      "\xE2\x98\x83"
      "d";
  static const size_t src4Len =
      ArrayLength(src4) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src4, src4Len), Span(buf, 4));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 2);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0x2603));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>('d'));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src5[] = "\xE2\x98\x83\xC3\xA4";
  static const size_t src5Len =
      ArrayLength(src5) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src5, src5Len), Span(buf, 5));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 2);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0x2603));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>(0xE4));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src6[] = "\xF0\x9F\x93\x8E";
  static const size_t src6Len =
      ArrayLength(src6) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src6, src6Len), Span(buf, 4));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 2);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0xD83D));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>(0xDCCE));
  MOZ_RELEASE_ASSERT(buf[2] == 0);

  static const char src7[] =
      "\xF0\x9F\x93\x8E"
      "e";
  static const size_t src7Len =
      ArrayLength(src7) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src7, src7Len), Span(buf, 5));
  MOZ_RELEASE_ASSERT(!written.isNothing());
  MOZ_RELEASE_ASSERT(*written == 3);
  MOZ_RELEASE_ASSERT(buf[0] == static_cast<char16_t>(0xD83D));
  MOZ_RELEASE_ASSERT(buf[1] == static_cast<char16_t>(0xDCCE));
  MOZ_RELEASE_ASSERT(buf[2] == static_cast<char16_t>('e'));
  MOZ_RELEASE_ASSERT(buf[3] == 0);

  static const char src8[] = "\xF0\x9F\x93";
  static const size_t src8Len =
      ArrayLength(src8) - 1;  // -1 for the nullptr byte at the end
  written =
      ConvertUtf8toUtf16WithoutReplacement(Span(src8, src8Len), Span(buf, 5));
  MOZ_RELEASE_ASSERT(written.isNothing());
}

static void DecodeValidUtf8(const char* bytes) {
  size_t bytesLen = strlen(bytes);
  MOZ_RELEASE_ASSERT(Utf8ValidUpTo(Span(bytes, bytesLen)) == bytesLen);
}

static void TestValidUtf8() {
  // Empty
  DecodeValidUtf8("");
  // ASCII
  DecodeValidUtf8("ab");
  // Low BMP
  DecodeValidUtf8("a\u00E4Z");
  // High BMP
  DecodeValidUtf8("a\u2603Z");
  // Astral
  DecodeValidUtf8("a\U0001F4A9Z");

  // Boundary conditions
  // Lowest single-byte
  DecodeValidUtf8("Z\x00");
  DecodeValidUtf8("Z\x00Z");

  // Highest single-byte
  DecodeValidUtf8("a\x7F");
  DecodeValidUtf8("a\x7FZ");
}

static void EncodeUtf8FromUtf16(Span<const char16_t> src,
                                Span<const char> expect) {
  const size_t dstLen = src.Length() * 3 + 1;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));
  const auto written = ConvertUtf16toUtf8(src, Span(dst, dstLen));
  MOZ_RELEASE_ASSERT(written == expect.Length());
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, expect.Elements(), written));
}

#define P99_PROTECT(...) __VA_ARGS__
#define ARR(...) P99_PROTECT(__VA_ARGS__)
#define ENC(src, expect)                                          \
  {                                                               \
    const char16_t src1[] = src;                                  \
    const char expect1[] = expect;                                \
    EncodeUtf8FromUtf16(Span(src1, ArrayLength(src1)),            \
                        Span(expect1, ArrayLength(expect1) - 1)); \
  }

#define ENC_WITH_EMPTY_SRC(expect)                                \
  {                                                               \
    const char16_t src1[] = {};                                   \
    const char expect1[] = expect;                                \
    EncodeUtf8FromUtf16(Span(src1, src1),                         \
                        Span(expect1, ArrayLength(expect1) - 1)); \
  }

static void EncodeUtf8FromUtf16WithOutputLimit(Span<const char16_t> src,
                                               Span<const char> expect,
                                               size_t limit,
                                               Maybe<size_t> shouldRead,
                                               Maybe<size_t> shouldWrite) {
  const size_t dstLen = limit;
  char dst[dstLen];
  memset(dst, 0, dstLen * sizeof(char));

  size_t read;
  size_t written;
  Tie(read, written) = ConvertUtf16toUtf8Partial(src, Span(dst, dstLen));
  MOZ_RELEASE_ASSERT(written <= limit);
  if (!shouldRead.isNothing()) {
    MOZ_RELEASE_ASSERT(read == *shouldRead);
  }
  if (!shouldWrite.isNothing()) {
    MOZ_RELEASE_ASSERT(written == *shouldWrite);
  }
  for (size_t i = 0; i < written; ++i) {
    const auto expectedChar = expect.Elements()[i];
    MOZ_RELEASE_ASSERT(expectedChar == dst[i]);
  }
  MOZ_RELEASE_ASSERT(IsUtf8(Span(dst, written)));
  MOZ_RELEASE_ASSERT(ArrayEqual(dst, expect.Elements(), written));
}

#define SBRC SINGLE_BYTE_REPLACEMENT_CHAR
#define DBRC DOUBLE_BYTE_REPLACEMENT_CHAR
#define TBRC TRIPLE_BYTE_REPLACEMENT_CHAR

#define ENC_LMT(src, expect, limit, read, written)                  \
  {                                                                 \
    const char16_t src1[] = src;                                    \
    const char expect1[] = expect;                                  \
    EncodeUtf8FromUtf16WithOutputLimit(                             \
        Span(src1, ArrayLength(src1)),                              \
        Span(expect1, ArrayLength(expect1) - 1), limit, Some(read), \
        Some(written));                                             \
  }

#define ENC_LMT_WITH_EMPTY_SOURCE(expect, limit, read, written)           \
  {                                                                       \
    const char16_t src1[] = {};                                           \
    const char expect1[] = expect;                                        \
    EncodeUtf8FromUtf16WithOutputLimit(                                   \
        Span(src1, src1), Span(expect1, ArrayLength(expect1) - 1), limit, \
        Some(read), Some(written));                                       \
  }
//     fn EncodeUtf8FromUtf16WithOutputLimit(
//         string: &[u16],
//         expect: &str,
//         limit: usize,
//         expect_result: EncoderResult,
//     ) {
//         let mut dst = Vec::new();
//         {
//             dst.resize(limit, 0u8);
//             let mut encoder = UTF_8.new_encoder();
//             let (result, read, written) =
//                 encoder.encode_from_utf16_without_replacement(string, &mut
//                 dst, false);
//             assert_eq!(result, expect_result);
//             if expect_result == EncoderResult::InputEmpty {
//                 assert_eq!(read, string.len());
//             }
//             assert_eq!(&dst[..written], expect.as_bytes());
//         }
//         {
//             dst.resize(64, 0u8);
//             for (i, elem) in dst.iter_mut().enumerate() {
//                 *elem = i as u8;
//             }
//             let mut encoder = UTF_8.new_encoder();
//             let (_, _, mut j) =
//                 encoder.encode_from_utf16_without_replacement(string, &mut
//                 dst, false);
//             while j < dst.len() {
//                 assert_eq!(usize::from(dst[j]), j);
//                 j += 1;
//             }
//         }
//     }

static void TestUtf8Encode() {
  // // Empty
  ENC_WITH_EMPTY_SRC("");
  ENC({0x0000}, "\u0000");
  ENC({0x007F}, "\u007F");
  ENC({0x0080}, "\u0080");
  ENC({0x07FF}, "\u07FF");
  ENC({0x0800}, "\u0800");
  ENC({0xD7FF}, "\uD7FF");
  ENC({0xD800}, TBRC);
  ENC(ARR({0xD800, 0x0062}), TBRC "\u0062");
  ENC({0xDFFF}, TBRC);
  ENC(ARR({0xDFFF, 0x0062}), TBRC "\u0062");
  ENC({0xE000}, "\uE000");
  ENC({0xFFFF}, "\uFFFF");
  ENC(ARR({0xD800, 0xDC00}), "\U00010000");
  ENC(ARR({0xDBFF, 0xDFFF}), "\U0010FFFF");
  ENC(ARR({0xDC00, 0xDEDE}), TBRC TBRC);
}

static void TestEncodeUtf8FromUtf16WithOutputLimit() {
  // Single-byte UTF-8 input.
  ENC_LMT({0x0062}, "", 0, 0, 0);
  ENC_LMT({0x0062}, "\u0062", 1, 1, 1);

  // Double-byte UTF-8 input.
  ENC_LMT({0x00A7}, "", 0, 0, 0);
  ENC_LMT({0x00A7}, SBRC, 1, 1, 1);
  ENC_LMT({0x00A7}, "\u00A7", 2, 1, 2);

  // Triple-byte UTF-8 input.
  ENC_LMT({0x2603}, "", 0, 0, 0);
  ENC_LMT({0x2603}, SBRC, 1, 1, 1);
  ENC_LMT({0x2603}, DBRC, 2, 1, 2);
  ENC_LMT({0x2603}, "\u2603", 3, 1, 3);

  // Quadraple-byte UTF-8 input.
  ENC_LMT(ARR({0xD83D, 0xDCA9}), "", 0, 0, 0);
  ENC_LMT(ARR({0xD83D, 0xDCA9}), SBRC, 1, 2, 1);
  ENC_LMT(ARR({0xD83D, 0xDCA9}), DBRC, 2, 2, 2);
  ENC_LMT(ARR({0xD83D, 0xDCA9}), TBRC, 3, 2, 3);
  ENC_LMT(ARR({0xD83D, 0xDCA9}), "\U0001F4A9", 4, 2, 4);

  // Valid UTF-8 input starting with a single-byte UTF-8 character.
  ENC_LMT(ARR({0x0063, 0x0062}), "\u0063\u0062", 2, 2, 2);
  ENC_LMT(ARR({0x0063, 0x00A7}), "\u0063" SBRC, 2, 2, 2);
  ENC_LMT(ARR({0x0063, 0x00A7}), "\u0063\u00A7", 3, 2, 3);

  ENC_LMT(ARR({0x0063, 0x2603}), "", 0, 0, 0);
  ENC_LMT(ARR({0x0063, 0x2603}), "\u0063", 1, 1, 1);
  ENC_LMT(ARR({0x0063, 0x2603}), "\u0063" SBRC, 2, 2, 2);
  ENC_LMT(ARR({0x0063, 0x2603}), "\u0063" DBRC, 3, 2, 3);
  ENC_LMT(ARR({0x0063, 0x2603}), "\u0063\u2603", 4, 2, 4);

  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "", 0, 0, 0);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063", 1, 1, 1);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063" SBRC, 2, 3, 2);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063" DBRC, 3, 3, 3);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063" TBRC, 4, 3, 4);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063\U0001F4A9", 5, 3, 5);
  ENC_LMT(ARR({0x0063, 0xD83D, 0xDCA9}), "\u0063\U0001F4A9", 6, 3, 5);

  // Valid UTF-8 input starting with a double-byte UTF-8 character.
  ENC_LMT(ARR({0x00B6, 0x0062}), "", 0, 0, 0);
  ENC_LMT(ARR({0x00B6, 0x0062}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x00B6, 0x0062}), "\u00B6", 2, 1, 2);
  ENC_LMT(ARR({0x00B6, 0x0062}), "\u00B6\u0062", 3, 2, 3);
  ENC_LMT(ARR({0x00B6, 0x0062}), "\u00B6\u0062", 4, 2, 3);

  ENC_LMT(ARR({0x00B6, 0x00A7}), "", 0, 0, 0);
  ENC_LMT(ARR({0x00B6, 0x00A7}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x00B6, 0x00A7}), "\u00B6", 2, 1, 2);
  ENC_LMT(ARR({0x00B6, 0x00A7}), "\u00B6" SBRC, 3, 2, 3);
  ENC_LMT(ARR({0x00B6, 0x00A7}), "\u00B6\u00A7", 4, 2, 4);
  ENC_LMT(ARR({0x00B6, 0x00A7}), "\u00B6\u00A7", 5, 2, 4);

  ENC_LMT(ARR({0x00B6, 0x2603}), "", 0, 0, 0);
  ENC_LMT(ARR({0x00B6, 0x2603}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x00B6, 0x2603}), "\u00B6", 2, 1, 2);
  ENC_LMT(ARR({0x00B6, 0x2603}), "\u00B6" SBRC, 3, 2, 3);
  ENC_LMT(ARR({0x00B6, 0x2603}), "\u00B6" DBRC, 4, 2, 4);
  ENC_LMT(ARR({0x00B6, 0x2603}), "\u00B6\u2603", 5, 2, 5);
  ENC_LMT(ARR({0x00B6, 0x2603}), "\u00B6\u2603", 6, 2, 5);

  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "", 0, 0, 0);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6", 2, 1, 2);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6" SBRC, 3, 3, 3);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6" DBRC, 4, 3, 4);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6" TBRC, 5, 3, 5);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6\U0001F4A9", 6, 3, 6);
  ENC_LMT(ARR({0x00B6, 0xD83D, 0xDCA9}), "\u00B6\U0001F4A9", 7, 3, 6);

  // Valid UTF-8 input starting with a triple-byte UTF-8 character.
  ENC_LMT(ARR({0x263A, 0x0062}), "", 0, 0, 0);
  ENC_LMT(ARR({0x263A, 0x0062}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x263A, 0x0062}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0x263A, 0x0062}), "\u263A", 3, 1, 3);
  ENC_LMT(ARR({0x263A, 0x0062}), "\u263A\u0062", 4, 2, 4);
  ENC_LMT(ARR({0x263A, 0x0062}), "\u263A\u0062", 5, 2, 4);

  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), "", 0, 0, 0);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), "\u263A", 3, 1, 3);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), "\u263A\u0062", 4, 2, 4);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), "\u263A\u0062\u0062", 5, 3, 5);
  ENC_LMT(ARR({0x263A, 0x0062, 0x0062}), "\u263A\u0062\u0062", 6, 3, 5);

  ENC_LMT(ARR({0x263A, 0x00A7}), "", 0, 0, 0);
  ENC_LMT(ARR({0x263A, 0x00A7}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x263A, 0x00A7}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0x263A, 0x00A7}), "\u263A", 3, 1, 3);
  ENC_LMT(ARR({0x263A, 0x00A7}), "\u263A" SBRC, 4, 2, 4);
  ENC_LMT(ARR({0x263A, 0x00A7}), "\u263A\u00A7", 5, 2, 5);
  ENC_LMT(ARR({0x263A, 0x00A7}), "\u263A\u00A7", 6, 2, 5);

  ENC_LMT(ARR({0x263A, 0x2603}), "", 0, 0, 0);
  ENC_LMT(ARR({0x263A, 0x2603}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x263A, 0x2603}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A", 3, 1, 3);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A" SBRC, 4, 2, 4);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A" DBRC, 5, 2, 5);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A\u2603", 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A\u2603", 7, 2, 6);

  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "", 0, 0, 0);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A", 3, 1, 3);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A" SBRC, 4, 3, 4);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A" DBRC, 5, 3, 5);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A" TBRC, 6, 3, 6);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A\U0001F4A9", 7, 3, 7);
  ENC_LMT(ARR({0x263A, 0xD83D, 0xDCA9}), "\u263A\U0001F4A9", 8, 3, 7);

  // Valid UTF-8 input starting with a triple-byte UTF-8 character.
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), "", 0, 0, 0);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), SBRC, 1, 2, 1);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), DBRC, 2, 2, 2);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), TBRC, 3, 2, 3);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), "\U0001F60E", 4, 2, 4);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), "\U0001F60E\u0062", 5, 3, 5);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x0062}), "\U0001F60E\u0062", 6, 3, 5);

  ENC_LMT(ARR({0xFFFD}), "", 0, 0, 0);
  ENC_LMT(ARR({0xFFFD}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0xFFFD}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0xFFFD}), "\uFFFD", 3, 1, 3);

  // Valid UTF-8 input starting with a quadruple-byte UTF-8 character and ending
  // with a double-byte UTF-8 character.
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), SBRC, 1, 2, 1);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), DBRC, 2, 2, 2);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), TBRC, 3, 2, 3);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), "\U0001F60E", 4, 2, 4);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), "\U0001F60E" SBRC, 5, 3, 5);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), "\U0001F60E\u00A7", 6, 3, 6);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x00A7}), "\U0001F60E\u00A7", 7, 3, 6);

  // Valid UTF-8 input starting with a quadruple-byte UTF-8 character and ending
  // with a triple-byte UTF-8 character.
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), SBRC, 1, 2, 1);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), DBRC, 2, 2, 2);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), TBRC, 3, 2, 3);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), "\U0001F60E", 4, 2, 4);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), "\U0001F60E" SBRC, 5, 3, 5);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), "\U0001F60E" DBRC, 6, 3, 6);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), "\U0001F60E\u2603", 7, 3, 7);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0x2603}), "\U0001F60E\u2603", 8, 3, 7);

  // Valid UTF-8 input starting with a quadruple-byte UTF-8 character and ending
  // with a quadruple-byte UTF-8 character.
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), SBRC, 1, 2, 1);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), DBRC, 2, 2, 2);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), TBRC, 3, 2, 3);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E", 4, 2, 4);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E" SBRC, 5, 4, 5);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E" DBRC, 6, 4, 6);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E" TBRC, 7, 4, 7);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E\U0001F4A9", 8, 4,
          8);
  ENC_LMT(ARR({0xD83D, 0xDE0E, 0xD83D, 0xDCA9}), "\U0001F60E\U0001F4A9", 9, 4,
          8);

  // Valid UTF-8 input with a double-byte UTF-8 character in the middle.
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063", 1, 1, 1);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063" SBRC, 2, 2, 2);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063\u00B6", 3, 2, 3);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063\u00B6\u0062", 4, 3, 4);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063\u00B6\u0062\u0062", 5,
          4, 5);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x0062}), "\u0063\u00B6\u0062\u0062", 6,
          4, 5);

  // Invalid UTF-8 code-units in the input
  ENC_LMT_WITH_EMPTY_SOURCE("", 0, 0, 0);
  ENC_LMT(ARR({0xD83D}), "", 0, 0, 0);
  ENC_LMT(ARR({0xD83D}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0xD83D}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0xD83D}), TBRC, 3, 1, 3);
  ENC_LMT(ARR({0xD83D}), TBRC, 4, 1, 3);

  ENC_LMT(ARR({0xDCA9}), "", 0, 0, 0);
  ENC_LMT(ARR({0xDCA9}), SBRC, 1, 1, 1);
  ENC_LMT(ARR({0xDCA9}), DBRC, 2, 1, 2);
  ENC_LMT(ARR({0xDCA9}), TBRC, 3, 1, 3);
  ENC_LMT(ARR({0xDCA9}), TBRC, 4, 1, 3);

  ENC_LMT(ARR({0x263A, 0xD83D}), "\u263A" TBRC, 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0xD83D}), "\u263A" TBRC, 7, 2, 6);

  ENC_LMT(ARR({0x263A, 0xDCA9}), "\u263A" TBRC, 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0xDCA9}), "\u263A" TBRC, 7, 2, 6);

  ENC_LMT(ARR({0x263A, 0xD83D, 0x00B6}), "\u263A" TBRC "\u00B6", 8, 3, 8);

  // Misc. Tests
  ENC_LMT(ARR({0x0063, 0x00B6, 0x00A7}), "\u0063\u00B6\u00A7", 5, 3, 5);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x00A7}), "\u0063\u00B6" SBRC, 4, 3, 4);

  ENC_LMT(ARR({0x0063, 0x00B6, 0x00A7, 0x0062}), "\u0063\u00B6\u00A7\u0062", 6,
          4, 6);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x00A7, 0x0062}), "\u0063\u00B6\u00A7", 5, 3, 5);

  ENC_LMT(ARR({0x263A, 0x00A7, 0x0062}), "\u263A\u00A7\u0062", 6, 3, 6);
  ENC_LMT(ARR({0x263A, 0x00A7, 0x0062}), "\u263A\u00A7", 5, 2, 5);

  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x00A7}), "\u0063\u00B6\u0062\u00A7", 6,
          4, 6);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x0062, 0x00A7}), "\u0063\u00B6\u0062" SBRC, 5,
          4, 5);

  ENC_LMT(ARR({0x263A, 0x0062, 0x00A7}), "\u263A\u0062\u00A7", 6, 3, 6);
  ENC_LMT(ARR({0x263A, 0x0062, 0x00A7}), "\u263A\u0062" SBRC, 5, 3, 5);

  ENC_LMT(ARR({0x0063, 0x00B6, 0x2603}), "\u0063\u00B6\u2603", 6, 3, 6);
  ENC_LMT(ARR({0x0063, 0x00B6, 0x2603}), "\u0063\u00B6" DBRC, 5, 3, 5);

  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A\u2603", 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0x2603}), "\u263A" DBRC, 5, 2, 5);

  ENC_LMT(ARR({0x0063, 0x00B6, 0xD83D}), "\u0063\u00B6" TBRC, 6, 3, 6);
  ENC_LMT(ARR({0x0063, 0x00B6, 0xD83D}), "\u0063\u00B6" DBRC, 5, 3, 5);

  ENC_LMT(ARR({0x263A, 0xD83D}), "\u263A" TBRC, 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0xD83D}), "\u263A" DBRC, 5, 2, 5);

  ENC_LMT(ARR({0x0063, 0x00B6, 0xDCA9}), "\u0063\u00B6" TBRC, 6, 3, 6);
  ENC_LMT(ARR({0x0063, 0x00B6, 0xDCA9}), "\u0063\u00B6" DBRC, 5, 3, 5);

  ENC_LMT(ARR({0x263A, 0xDCA9}), "\u263A" TBRC, 6, 2, 6);
  ENC_LMT(ARR({0x263A, 0xDCA9}), "\u263A" DBRC, 5, 2, 5);
}

#undef P99_PROTECT
#undef ARR
#undef ENC
#undef ENC_LMT
#undef SBRC
#undef SBRC
#undef DBRC
#undef TBRC

int main() {
  TestUtf8Unit();
  TestIsUtf8();
  TestDecodeOneUtf8CodePoint();
  TestUtf8ValidUpTo();
  TestConvertUtf16toUtf8Partial();
  TestConvertUtf16toUtf8();
  TestConvertUtf8toUtf16();
  TestConvertUtf8toUtf16WithoutReplacement();
  TestValidUtf8();
  TestUtf8Encode();
  TestEncodeUtf8FromUtf16WithOutputLimit();
  return 0;
}

#if defined(__clang__) && (__clang_major__ >= 6)
#  pragma clang diagnostic pop
#endif
