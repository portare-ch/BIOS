/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Variables - the menu's settings as UEFI non-volatile variables.

  XBL keeps variables in the TrustZone variable app, which writes them to
  the varstore partition only when flushed (QcomFlushVariables). The Boot
  order is the only setting; the menu also keeps its one-shot choices here
  (Menu/Menu.h).
*/

#ifndef __EFI_VARIABLES_H__
#define __EFI_VARIABLES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/Log.h"

// The settings' vendor GUID, shared by the ABL and the menu:
// 8c5f2b7e-3a41-4d6c-9e2f-5b7a1c0d4e93.
#define SETTINGS_VENDOR_GUID                                                   \
  {0x8c5f2b7e, 0x3a41, 0x4d6c, {0x9e, 0x2f, 0x5b, 0x7a, 0x1c, 0x0d, 0x4e, 0x93}}

// Reads a setting's value. FALSE if it is unset or unreadable. Caller frees
// Out with BufferFree.
BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Writes a setting as a non-volatile variable, without flushing. Length 0
// deletes it, and deleting a missing setting succeeds. EFI_INVALID_PARAMETER
// for a name UCS-2 can't hold.
EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
);

// The Boot order: one byte, nonzero to start Android first. Anything else,
// or nothing, starts PortareOS first.
#define VARIABLES_BOOT_MODE "BootMode"

// Returns TRUE if the Boot order starts Android first.
BOOLEAN
VariablesAndroidFirst (VOID);

#endif
