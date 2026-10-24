/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Dxe - GCD memory space calls from the DXE services table (PI spec, vol. 2).

  The XBL quirks use them to make a page of driver code writable. Every
  call fails if the firmware has no DXE services table.
*/

#ifndef __EFI_DXE_H__
#define __EFI_DXE_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Pi/PiDxeCis.h>

#include "Base/Buffer.h"

// Gets the GCD descriptor of the space holding Address.
BOOLEAN
DxeDescriptor (
  IN UINT64                            Address,
  OUT EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Out
);

EFI_STATUS
DxeSetAttributes (
  IN UINT64 Base,
  IN UINT64 Length,
  IN UINT64 Attributes
);

#endif
