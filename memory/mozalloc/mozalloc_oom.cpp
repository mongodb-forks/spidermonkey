/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=4 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// MONGODB MODIFICATION: Replace the mozalloc header chain with minimal
// includes. mozalloc_abort.cpp redefines abort() process-wide on Unix, which
// is not appropriate for embedding. We call fputs + std::abort() directly.
#include "mozilla/Types.h"
#include <cstdlib>
#include <stdio.h>

#define OOM_MSG_LEADER "out of memory: 0x"
#define OOM_MSG_DIGITS "0000000000000000"  // large enough for 2^64
#define OOM_MSG_TRAILER " bytes requested"
#define OOM_MSG_FIRST_DIGIT_OFFSET sizeof(OOM_MSG_LEADER) - 1
#define OOM_MSG_LAST_DIGIT_OFFSET \
  sizeof(OOM_MSG_LEADER) + sizeof(OOM_MSG_DIGITS) - 3

MFBT_DATA size_t gOOMAllocationSize = 0;

static const char* hex = "0123456789ABCDEF";

MFBT_API void mozalloc_handle_oom(size_t size) {
  char oomMsg[] = OOM_MSG_LEADER OOM_MSG_DIGITS OOM_MSG_TRAILER;
  size_t i;

  gOOMAllocationSize = size;

  static_assert(OOM_MSG_FIRST_DIGIT_OFFSET > 0,
                "Loop below will never terminate (i can't go below 0)");

  // Insert size into the diagnostic message using only primitive operations.
  for (i = OOM_MSG_LAST_DIGIT_OFFSET; size && i >= OOM_MSG_FIRST_DIGIT_OFFSET;
       i--) {
    oomMsg[i] = hex[size % 16];
    size /= 16;
  }

  // MONGODB MODIFICATION: Write the message and abort directly instead of
  // calling mozalloc_abort().
  fputs(oomMsg, stderr);
  fputs("\n", stderr);
  std::abort();
}
