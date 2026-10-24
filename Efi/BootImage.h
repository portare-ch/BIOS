/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootImage - finds and starts PortareOS's boot image, an Android boot image
  (header v0 to v2) at \KERNEL on its boot partition.

  The image is read into page-rounded, zeroed pool memory. After a
  successful SetBootImage the ABL owns it; otherwise the caller frees it
  with gBS->FreePool. BootImageStart lays it out as Qualcomm's BootLinux
  does, with the Nova's devicetree from those appended to the kernel, then
  the ABL updates the devicetree and jumps to the kernel.
*/

#ifndef __EFI_BOOT_IMAGE_H__
#define __EFI_BOOT_IMAGE_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/EfiData.h"
#include "Efi/MenuHost.h"
#include "Efi/Log.h"

// Where a drive's boot image is.
#define BOOT_IMAGE_PATH "\\KERNEL"
// The model property of the devicetree BootImageStart picks.
#define BOOT_IMAGE_MODEL "Retroid Pocket Nova"

// A boot image in pool memory.
typedef struct _BOOT_IMAGE_FOUND {
  UINT8 *Buffer;
  UINTN  Size;
  // The GPT partition it was found on, for the kernel's portare.bootpart,
  // or "".
  CHAR8 PartUuid[UEFI_PART_UUID_SIZE];
} BOOT_IMAGE_FOUND;

// Returns TRUE if Fs has a file at BOOT_IMAGE_PATH that starts with the
// Android boot image magic.
BOOLEAN
BootImageIsOn (IN EFI_HANDLE Fs);

// Finds the first of Drives with a boot image, in order. If XBL's FAT
// driver can't read or can't see the file, it is read straight off FAT32.
BOOLEAN
BootImageFind (
  IN CONST EFI_HANDLE  *Drives,
  IN UINTN              Count,
  IN LOG_SINK           Log,
  OUT BOOT_IMAGE_FOUND *Out
);

// Starts the boot image passed to SetBootImage. Verified boot
// (OPENABL_MENU_ACTION_BOOT_IMAGE_VERIFY) checks it first; after that the
// buffer is ours again and is freed here. Returns only if the kernel didn't
// start.
VOID
BootImageStart (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN LOG_SINK                Log
);

// Like BootImageStart, for an image that is already verified (fastboot's
// boot command while PortareOS starts first). The buffer stays the
// caller's. Returns EFI_LOAD_ERROR, only on failure.
EFI_STATUS
BootImageStartVerified (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN LOG_SINK                Log
);

#endif
