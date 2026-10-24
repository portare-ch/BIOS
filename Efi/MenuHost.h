/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MenuHost - the protocol the ABL offers the menu it embeds
  (Protocol/OpenAblMenuHost.h in the ABL).

  The ABL installs it on its image handle and starts the menu from its
  firmware volume. The menu draws, reads keys and keeps the settings. The ABL
  reports its state, restarts and powers off, and serves fastboot on each
  Poll.
*/

#ifndef __EFI_MENU_HOST_H__
#define __EFI_MENU_HOST_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#include "Core/BootDecision.h"

// Why the last start of PortareOS came back, or why the menu opens.
#define MENU_START_RESULT_NONE 0u
// Nothing to boot.
#define MENU_START_RESULT_NOTHING 1u
// The boot image didn't verify or didn't start.
#define MENU_START_RESULT_FAILED 4u
// What the boot list picked is on a drive that's gone (the SD card taken
// out), so nothing was started.
#define MENU_START_RESULT_PICK_GONE 6u
// What the boot list picked didn't start, so nothing else was tried.
#define MENU_START_RESULT_PICK_FAILED 7u
// Not a failure: the charger powered the device on, so the menu shows its
// charging screen.
#define MENU_START_RESULT_CHARGING 8u
// Android's start came back.
#define MENU_START_RESULT_ANDROID_FAILED 0x80u

// Returns an OPENABL_MENU_ACTION_* name for the log, or "?".
CONST CHAR8 *
MenuHostActionName (IN UINT32 Action);

// The ABL's state, and the settings the menu decides from it.
typedef struct _MENU_HOST_STATE {
  UINT8  OffmodeCharging; // Qualcomm's charger screen flag
  UINT8  Flags;           // OPENABL_MENU_STATE_*
  UINT8  Entry;           // OPENABL_MENU_ENTRY_*
  UINT16 Key;             // the key held at the start, a SCAN_* code
  CHAR8  Version[32];     // the ABL's version
  // 1 boots Android: the Boot order says so, or Volume Up was held.
  UINT8 BootMode;
} MENU_HOST_STATE;


// A located host. The host keeps the protocol installed while the menu runs,
// so a saved Protocol pointer P can be used again as (MENU_HOST){P}.
typedef struct _MENU_HOST {
  OPENABL_MENU_HOST_PROTOCOL *Protocol;
} MENU_HOST;

// Finds the protocol on the first handle that has it. FALSE if the menu
// wasn't started by the ABL.
BOOLEAN
MenuHostLocate (OUT MENU_HOST *Out);

// Returns the protocol's OPENABL_MENU_FLAG_* bits.
STATIC inline UINT32
MenuHostFlags (IN CONST MENU_HOST *Host)
{
  return Host->Protocol->Flags;
}

// Reads the ABL's state and decides the settings from it.
EFI_STATUS
MenuHostState (
  IN CONST MENU_HOST  *Host,
  OUT MENU_HOST_STATE *Out
);

// Calls DoAction with an OPENABL_MENU_ACTION_* code.
EFI_STATUS
MenuHostAction (
  IN CONST MENU_HOST *Host,
  IN UINT32           Action
);

// Serves fastboot (Poll). Anything but EFI_SUCCESS means leave the menu.
EFI_STATUS
MenuHostPoll (IN CONST MENU_HOST *Host);

// Sends a line to the ABL's debug log.
VOID
MenuHostLog (
  IN CONST MENU_HOST *Host,
  IN CONST CHAR8     *Line
);

// Updates the devicetree and starts the kernel with Qualcomm's code. Returns
// only if the kernel didn't start.
EFI_STATUS
MenuHostStartKernel (
  IN CONST MENU_HOST      *Host,
  IN CONST OPENABL_KERNEL *Kernel
);

// Hands the host a boot image in pool memory; the host owns it from then on.
EFI_STATUS
MenuHostSetBootImage (
  IN CONST MENU_HOST *Host,
  IN VOID            *Buffer,
  IN UINTN            Size
);

// Returns a NUL-padded state field of Size bytes as UTF-8, up to its first
// NUL, with invalid bytes replaced. Caller frees it; NULL only when out of
// memory.
CHAR8 *
MenuHostText (
  IN CONST CHAR8 *Field,
  IN UINTN        Size
);

// Returns what the ABL read at start: the key and Entry's fastboot and
// recovery bits.
BOOT_DECISION_START
MenuHostStateStart (IN CONST MENU_HOST_STATE *State);

#endif
