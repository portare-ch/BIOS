/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Battery - starts charging and reads charger presence and battery voltage.
*/

#include "Efi/Battery.h"

#include <Protocol/EFIChargerEx.h>

// ChargerEx only reports battery state. Charging itself is controlled by
// QcomCharger. Only its stable prefix is needed; ABL has no header for it.
STATIC EFI_GUID mQcomChargerGuid = {
  0x21bfb78b,
  0x02ea,
  0x4ab7,
  {0x99, 0x40, 0xb5, 0x8a, 0x2a, 0xd6, 0x58, 0x29}};

typedef struct _QCOM_CHARGER {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *EnableCharging) (IN BOOLEAN Enable);
} QCOM_CHARGER;

#define QCOM_CHARGER_MIN_REVISION 0x0000000000010000

EFI_STATUS
BatteryStartCharging (VOID)
{
  QCOM_CHARGER *Charger = LocateProtocol (&mQcomChargerGuid);

  if (!Charger || Charger->Revision < QCOM_CHARGER_MIN_REVISION ||
      !Charger->EnableCharging) {
    return EFI_UNSUPPORTED;
  }
  return Charger->EnableCharging (TRUE);
}

BOOLEAN
BatteryRead (OUT BATTERY_STATE *Out)
{
  EFI_CHARGER_EX_PROTOCOL *Charger = LocateProtocol (&gChargerExProtocolGuid);
  BOOLEAN                  Present = FALSE;
  UINT32                   Voltage = 0;

  *Out = (BATTERY_STATE){0};
  if (!Charger || Charger->GetChargerPresence (&Present) != EFI_SUCCESS) {
    return FALSE;
  }
  if (Charger->GetBatteryVoltage (&Voltage) != EFI_SUCCESS) {
    Voltage = 0;
  }
  *Out = (BATTERY_STATE){.Charger = Present, .MilliVolts = Voltage};
  return TRUE;
}
