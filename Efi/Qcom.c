/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Qcom - calls into Qualcomm's XBL protocols, made as LinuxLoader makes them.
*/

#include "Efi/Qcom.h"

// EFI_QCOM_VARIABLESERVICES_PROTOCOL, which has no header in the ABL's tree.
STATIC EFI_GUID mVariableServicesGuid = {
  0xf9085b9d,
  0x9304,
  0x40fb,
  {0x8f, 0xe0, 0x4a, 0xee, 0x3b, 0x1a, 0x78, 0x4b}};

typedef struct _VARIABLE_SERVICES {
  UINT64 Revision;
  EFI_STATUS (EFIAPI *FlushVariableNv) (IN struct _VARIABLE_SERVICES *This);
} VARIABLE_SERVICES;

BOOLEAN
QcomFlushVariables (OUT EFI_STATUS *Status)
{
  VARIABLE_SERVICES *Services = LocateProtocol (&mVariableServicesGuid);

  if (!Services) {
    return FALSE;
  }
  *Status = Services->FlushVariableNv (Services);
  return TRUE;
}
