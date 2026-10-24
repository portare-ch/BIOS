/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Log - the log sink (Efi/Log.h).
*/

#include "Efi/Log.h"

#include "Base/Memory.h"
#include "Base/Print.h"

VOID
LogPrint (
  IN LOG_SINK     Log,
  IN CONST CHAR8 *Format,
  ...
)
{
  va_list Args;
  CHAR8  *Line;

  if (!Log.Fn) {
    return;
  }
  va_start (Args, Format);
  Line = VSPrintAlloc (Format, Args);
  va_end (Args);
  if (!Line) {
    return;
  }
  Log.Fn (Log.Ctx, Line);
  FreePool (Line);
}
