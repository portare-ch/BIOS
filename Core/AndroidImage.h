/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  AndroidImage - Android partition formats: boot images (header v0-v4), the
  bootloader message in misc, and devicetrees inside images.
*/

#ifndef __CORE_ANDROID_IMAGE_H__
#define __CORE_ANDROID_IMAGE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"

// Not NUL-terminated in the image.
#define ANDROID_BOOT_MAGIC "ANDROID!"

// A section of a boot image. Offset is from the start of the image.
typedef struct _ANDROID_SECTION {
  UINT64 Offset;
  UINT32 Size;
} ANDROID_SECTION;

typedef struct _ANDROID_OS_VERSION {
  UINT8  Major, Minor, Patch;
  UINT16 Year;
  UINT8  Month;
} ANDROID_OS_VERSION;

typedef struct _ANDROID_BOOT_IMAGE {
  UINT32          HeaderVersion;
  UINT32          PageSize;
  ANDROID_SECTION Kernel;
  ANDROID_SECTION Ramdisk;
  ANDROID_SECTION Second;
  ANDROID_SECTION RecoveryDtbo;
  ANDROID_SECTION Dtb;
  // Freed by AndroidBootImageFree.
  CHAR8             *Cmdline;
  BOOLEAN            HasOsVersion;
  ANDROID_OS_VERSION OsVersion;
} ANDROID_BOOT_IMAGE;

// Parses a boot image header. Header must hold at least the first 4 KiB of
// the image. Free the result with AndroidBootImageFree.
BOOLEAN
AndroidBootImageParse (
  IN CONST UINT8         *Header,
  IN UINTN                Size,
  OUT ANDROID_BOOT_IMAGE *Out
);

VOID
AndroidBootImageFree (IN OUT ANDROID_BOOT_IMAGE *Image);

// Decodes an os_version: 7 bits each of A.B.C, then 7 bits of year - 2000
// and 4 bits of month. Fails if the value is 0 (not set).
BOOLEAN
AndroidDecodeOsVersion (
  IN UINT32               Value,
  OUT ANDROID_OS_VERSION *Out
);

// Length of the command field of the bootloader message in misc.
#define ANDROID_BCB_COMMAND_LEN 32

typedef enum {
  ANDROID_BOOT_COMMAND_NONE,
  // boot-recovery
  ANDROID_BOOT_COMMAND_RECOVERY,
  // bootonce-bootloader: stop in the bootloader once.
  ANDROID_BOOT_COMMAND_BOOTLOADER_ONCE,
  ANDROID_BOOT_COMMAND_OTHER,
} ANDROID_BOOT_COMMAND_KIND;

// The command field of struct bootloader_message (first 32 bytes of misc).
typedef struct _ANDROID_BOOT_COMMAND {
  ANDROID_BOOT_COMMAND_KIND Kind;
  // The command text for ANDROID_BOOT_COMMAND_OTHER, otherwise "".
  CHAR8 Other[ANDROID_BCB_COMMAND_LEN + 1];
} ANDROID_BOOT_COMMAND;

typedef struct _ANDROID_DTB {
  UINTN Offset, Size;
} ANDROID_DTB;

typedef struct _ANDROID_DTBS OABL_VEC (ANDROID_DTB) ANDROID_DTBS;

// Finds every devicetree with a model in a DTB section or a kernel tail. On
// failure, Found keeps the blobs found so far.
BOOLEAN
AndroidFindDtbs (
  IN CONST UINT8   *Region,
  IN UINTN          Size,
  OUT ANDROID_DTBS *Found
);

// How much of the end of a kernel to search first for appended devicetrees.
// Same as LinuxLoader's MODEL_DTB_TAIL_WINDOW.
#define ANDROID_DTB_TAIL_WINDOW ((UINT64)4 << 20)

#endif
