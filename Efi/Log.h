/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Log - the log sink the Efi modules pass around. The menu's sink writes to
  the ABL's log.
*/

#ifndef __EFI_LOG_H__
#define __EFI_LOG_H__

#include "Base/OablBase.h"

// Where a module's log lines go. Passed by value. Fn gets each line
// NUL-terminated, valid only for the call. A NULL Fn drops every line.
typedef struct _LOG_SINK {
  VOID (*Fn) (
    IN VOID        *Context OPTIONAL,
    IN CONST CHAR8 *Line
  );
  VOID *Ctx;
} LOG_SINK;

#define LOG_QUIET ((LOG_SINK){NULL, NULL})

STATIC inline VOID
LogLine (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Line
)
{
  if (Log.Fn) {
    Log.Fn (Log.Ctx, Line);
  }
}

// Writes a formatted line to Log. Nothing is logged if allocating the text
// fails.
VOID
LogPrint (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Format,
  ...
)
  __attribute__ ((format (
    printf,
    2,
    3
  )));

#endif
