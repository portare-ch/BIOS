/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Variables - the menu's settings as UEFI variables (Efi/Variables.h).
*/

#include "Efi/Variables.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Efi/Efi.h"

STATIC EFI_GUID mVendor = SETTINGS_VENDOR_GUID;

// Converts Name to NUL-terminated UCS-2; FreePool the result.
// EFI_INVALID_PARAMETER for a character outside the BMP,
// EFI_OUT_OF_RESOURCES when out of memory.
STATIC EFI_STATUS
Ucs2 (
  IN CONST CHAR8 *Name,
  OUT CHAR16    **Out
)
{
  UINT16 *Units = Utf16FromStr (Name);

  *Out = NULL;
  if (!Units) {
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN Index = 0; Units[Index]; Index++) {
    if (Units[Index] >= 0xD800 && Units[Index] <= 0xDFFF) {
      FreePool (Units);
      return EFI_INVALID_PARAMETER;
    }
  }
  *Out = (CHAR16 *)Units;
  return EFI_SUCCESS;
}

BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  CHAR16    *VariableName;
  UINT8      Probe;
  UINTN      Size       = 0;
  UINT32     Attributes = 0;
  EFI_STATUS Status;

  *Out = (OABL_BUFFER){0};
  if (Ucs2 (Name, &VariableName) != EFI_SUCCESS) {
    return FALSE;
  }
  // Ask for the size first; it must answer EFI_BUFFER_TOO_SMALL.
  Status =
    gRT->GetVariable (VariableName, &mVendor, &Attributes, &Size, &Probe);
  if (Status != EFI_BUFFER_TOO_SMALL || Size == 0 ||
      !BufferAppendZeros (Out, Size)) {
    FreePool (VariableName);
    return FALSE;
  }
  Status =
    gRT->GetVariable (VariableName, &mVendor, &Attributes, &Size, Out->Data);
  FreePool (VariableName);
  if (Status != EFI_SUCCESS) {
    BufferFree (Out);
    return FALSE;
  }
  BufferTruncate (Out, Size);
  return TRUE;
}

EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  UINT8      None = 0;
  CHAR16    *VariableName;
  EFI_STATUS Status = Ucs2 (Name, &VariableName);

  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (Length == 0) {
    Status = gRT->SetVariable (VariableName, &mVendor, 0, 0, &None);
  } else {
    Status = gRT->SetVariable (
      VariableName, &mVendor,
      EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
        EFI_VARIABLE_RUNTIME_ACCESS,
      Length, (VOID *)Data
    );
  }
  FreePool (VariableName);
  // Deleting a missing variable counts as success.
  if (Status == EFI_SUCCESS || (Length == 0 && Status == EFI_NOT_FOUND)) {
    return EFI_SUCCESS;
  }
  return Status;
}

BOOLEAN
VariablesAndroidFirst (VOID)
{
  OABL_BUFFER Stored = {0};
  BOOLEAN     Android =
    VariablesGet (VARIABLES_BOOT_MODE, &Stored) && Stored.Len == 1 &&
    Stored.Data[0] != 0;

  BufferFree (&Stored);
  return Android;
}
