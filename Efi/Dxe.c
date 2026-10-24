/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Dxe - GCD memory space calls from the DXE services table (PI spec, vol. 2).
*/

#include "Efi/Dxe.h"

#include <Guid/DxeServices.h>

// Finds the DXE services table. It is only valid while boot services run.
STATIC DXE_SERVICES *
ServicesTable (VOID)
{
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE *Entry = &gST->ConfigurationTable[Index];
    if (CompareGuid (&Entry->VendorGuid, &gEfiDxeServicesTableGuid)) {
      return Entry->VendorTable;
    }
  }
  return NULL;
}

BOOLEAN
DxeDescriptor (
  IN UINT64                            Address,
  OUT EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Out
)
{
  DXE_SERVICES *Services = ServicesTable ();

  *Out = (EFI_GCD_MEMORY_SPACE_DESCRIPTOR){0};
  return Services &&
         Services->GetMemorySpaceDescriptor (Address, Out) == EFI_SUCCESS;
}

EFI_STATUS
DxeSetAttributes (
  IN UINT64 Base,
  IN UINT64 Length,
  IN UINT64 Attributes
)
{
  DXE_SERVICES *Services = ServicesTable ();

  return Services
           ? Services->SetMemorySpaceAttributes (Base, Length, Attributes)
           : EFI_UNSUPPORTED;
}
