/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Handover - what the system needs just before Linux starts: the
  hypervisor stops logging to the UART the kernel takes over, and the
  display region XBL scans out from is known, to keep it from the kernel.
*/

#ifndef __EFI_HANDOVER_H__
#define __EFI_HANDOVER_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

// Asks the hypervisor to stop logging to the UART the OS takes over. Same
// call as the ABL's DisableHypUartUsageForLogging: SMCCC fast call
// 0x86000000, which may clobber x0-x17.
VOID
HandoverHypUartOff (VOID);

// Gets XBL's splash framebuffer region, "Display Reserved" or
// "Display_Reserved". The panel may still be scanning it out.
BOOLEAN
HandoverDisplayRegion (
  OUT UINT64 *Base,
  OUT UINT64 *Len
);

#endif
