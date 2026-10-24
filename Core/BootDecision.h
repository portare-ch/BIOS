/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootDecision - what power-on starts, from the state read at ABL start,
  and the order of the drives PortareOS starts from.

  The inputs are the key held (as Qualcomm's GetKeyPress reads it),
  Qualcomm's fastboot and recovery flags and the Boot order.
*/

#ifndef __CORE_BOOT_DECISION_H__
#define __CORE_BOOT_DECISION_H__

#include "Base/OablBase.h"


// UEFI SCAN_* codes.
#define BOOT_DECISION_SCAN_UP     0x01
#define BOOT_DECISION_SCAN_DOWN   0x02
#define BOOT_DECISION_SCAN_HOME   0x05
#define BOOT_DECISION_SCAN_DELETE 0x08

typedef struct _BOOT_DECISION_START {
  // SCAN_* code of the key held, or 0.
  UINT16 Key;
  // Set by Volume Down or the reboot reason.
  BOOLEAN Fastboot;
  // Set by Volume Up, the reboot reason or the misc partition.
  BOOLEAN Recovery;
} BOOT_DECISION_START;

// Returns TRUE for a fastboot boot: Qualcomm's flag, or Delete held.
BOOLEAN
BootDecisionFastboot (IN CONST BOOT_DECISION_START *Start);

// Returns TRUE if Volume Up or Home is held on a boot that isn't fastboot.
BOOLEAN
BootDecisionUpHeld (IN CONST BOOT_DECISION_START *Start);

// Returns TRUE to start Android: the Boot order says so, or Volume Up or
// Home is held. A charger boot shows BIOS's charging screen instead.
BOOLEAN
BootDecisionAndroid (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting
);

// Returns TRUE to boot Android's recovery instead of its normal boot. In the
// Android boot mode, Qualcomm's recovery flag or Home held selects it. In
// the Linux boot mode, Volume Up boots Android itself, and since Qualcomm's
// flag can't tell Volume Up from its other sources, Volume Up wins.
BOOLEAN
BootDecisionRecovery (
  IN CONST BOOT_DECISION_START *Start,
  IN BOOLEAN                    AndroidSetting
);

// Boot sources: the drives PortareOS starts from, internal storage first,
// then the SD card. USB drives aren't booted.

typedef struct _BOOT_SOURCE_MEDIUM {
  // Removable media, or on a USB or SD path.
  BOOLEAN External;
  BOOLEAN Usb;
} BOOT_SOURCE_MEDIUM;

// Starts a drive's medium from BlockIo's RemovableMedia. Then pass each node
// of the drive's device path to BootSourceMediumNode.
BOOT_SOURCE_MEDIUM
BootSourceMediumOf (IN BOOLEAN Removable);

VOID
BootSourceMediumNode (
  IN OUT BOOT_SOURCE_MEDIUM *Medium,
  IN UINT8                   Type,
  IN UINT8                   SubType
);

BOOLEAN
BootSourceMediumInternal (IN BOOT_SOURCE_MEDIUM Medium);

// Fills Order (room for Count) with the indices of the drives to try,
// internal ones first and no USB drive, and returns how many.
UINTN
BootSourceSelect (
  IN CONST BOOT_SOURCE_MEDIUM *Media,
  IN UINTN                     Count,
  OUT UINTN                   *Order
);

#endif
