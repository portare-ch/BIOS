/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Menu - declarations shared by the menu program.

  Main.c is the entry point and runs the boot; Menu.c is the menu itself.
*/

#ifndef __MENU_MENU_H__
#define __MENU_MENU_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/MenuHost.h"
#include "Efi/Log.h"

// The ABL's menu-host protocol, set once at start so anything can log.
extern MENU_HOST gMenuHost;
extern BOOLEAN   gMenuHostSet;

// Set while fastboot runs.
extern BOOLEAN gFastbootUp;

// Writes a line, prefixed "openabl-menu: ", to the ABL's debug log. Log
// events, not every frame or key.
VOID
MenuLog (
  IN CONST CHAR8 *Fmt,
  ...
)
  __attribute__ ((format (
    printf,
    1,
    2
  )));

// MenuLog as a LOG_SINK, for the Efi modules' log lines.
extern CONST LOG_SINK gMenuEfiLog;

#define MENU_STATUS_SIZE 24

// Writes Status to Out as its low bits, prefixed "error " for errors, so
// EFI_NOT_FOUND gives "error 14". Returns Out.
CONST CHAR8 *
MenuStatus (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[MENU_STATUS_SIZE]
);

// MenuStatus into a buffer that lasts as long as the enclosing block.
#define MENU_STATUS(Status) MenuStatus ((Status), (CHAR8[MENU_STATUS_SIZE]){0})

// Reads a menu variable. Returns FALSE if unset. Caller frees Out.
BOOLEAN
MenuVarGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Writes a menu variable without flushing. An empty value deletes it, and
// deleting a missing variable is not an error.
EFI_STATUS
MenuVarPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
);

// Commits written variables to flash (XBL's FlushVariableNV).
VOID
MenuFlush (VOID);

typedef enum {
  MENU_END_LEFT,       // the ABL asked the menu to leave
  MENU_END_START_HERE, // PortareOS was chosen, booting without a restart
  MENU_END_FAILED,     // the menu couldn't run
} MENU_END_KIND;

typedef struct _MENU_END {
  MENU_END_KIND Kind;
  EFI_STATUS    Status;
} MENU_END;

// Runs the menu until the ABL asks it to leave or PortareOS is chosen.
// CameBack is how the last start returned, or why the menu opens
// (MENU_START_RESULT_*).
MENU_END
MenuRun (
  IN CONST MENU_HOST *Host,
  IN UINT8            CameBack
);

// The drive the boot list picked for the next start, kept across a
// restart: its device path. The start takes it and clears it.
#define MENU_PICK_VAR "BootEntryOnce"

// The system the menu picked for the next boot, kept across a restart: one
// MENU_ONCE_* byte. The boot takes it and clears it, so it applies once and
// leaves the Boot order as it was.
#define MENU_ONCE_VAR     "BootTargetOnce"
#define MENU_ONCE_LINUX   1
#define MENU_ONCE_ANDROID 2

// Lists the drives PortareOS starts from, in order. Caller frees *Volumes.
VOID
MenuBootVolumes (
  OUT EFI_HANDLE **Volumes,
  OUT UINTN       *Count
);

#endif
