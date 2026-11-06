/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Battery - starts charging through XBL's QcomCharger protocol and reads
  charger presence and voltage through ChargerEx. ChargerEx reports no
  charge level; the app estimates one from the voltage.
*/

#ifndef __EFI_BATTERY_H__
#define __EFI_BATTERY_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

typedef struct _BATTERY_STATE {
  BOOLEAN Charger;    // a charger is in
  UINT32  MilliVolts; // 0 where XBL can't read it
} BATTERY_STATE;

// Requests charging when entering the off-mode screen. Charger presence
// alone doesn't enable it. EFI_UNSUPPORTED without QcomCharger.
EFI_STATUS
BatteryStartCharging (VOID);

// FALSE where XBL has no charger protocol or can't tell if a charger is in.
BOOLEAN
BatteryRead (OUT BATTERY_STATE *Out);

#endif
