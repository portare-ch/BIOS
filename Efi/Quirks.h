/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Quirks - live patches for bugs in XBL's SD card driver, and the storage
  protocol the early copy of the menu leaves installed for the ABL.
*/

#ifndef __EFI_QUIRKS_H__
#define __EFI_QUIRKS_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Protocol/OpenAblMenuHost.h>

#include "Efi/Log.h"

// XBL's SdccDxe asks every SD card for 1.8 V signalling (S18R in ACMD41)
// but never sends CMD11, so UHS cards that wait for it are never found.
// Clears S18R at every ACMD41 site in firmware code outside this image (at
// most 16). With S18R clear, the driver would ask a card for the slot's UHS
// speed, so it asks for high speed instead. A card gets one second to power
// up; that becomes four while card detection is signalled again, then the
// second is put back. Returns the number of S18R sites patched.
UINT32
QuirksSdccUhs (IN LOG_SINK Log);

// Signals XBL's SD card detection event group
// (b7972c36-8a4c-4a56-8b02-1159b52d4bfb).
VOID
QuirksSignalSdDetect (VOID);

// Gets storage ready to read: signals SD card detection, then connects
// every controller.
VOID
QuirksPrepareStorage (VOID);

// Applies the devicetree fix-ups for a kernel: XBL's splash memory is
// reserved. Fdt may grow up to Cap bytes.
VOID
QuirksFixupDeviceTree (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
);

// Sets the entry point Run calls. Call before QuirksInstallProtocol.
VOID
QuirksSetRun (IN OPENABL_STORAGE_RUN Run);

// Writes "openabl-menu: " and Line to the ABL's logger. Does nothing if Log is
// NULL.
VOID
QuirksToAbl (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Line
);

// Returns a LOG_SINK that writes through QuirksToAbl to Log, which may be
// NULL.
LOG_SINK
QuirksAblLog (IN OPENABL_STORAGE_LOG Log OPTIONAL);

// Installs the ABL's storage protocol (OPENABL_STORAGE_PROTOCOL) on a new
// handle for the rest of boot services, so the menu must stay loaded.
EFI_STATUS
QuirksInstallProtocol (VOID);

#endif
