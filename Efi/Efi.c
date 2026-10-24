/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Efi - global tables, protocol helpers, GUIDs, status text and the clock.
*/

#include "Efi/Efi.h"

#include "Base/Memory.h"
#include "Base/Print.h"

EFI_HANDLE            gImageHandle;
EFI_SYSTEM_TABLE     *gST;
EFI_BOOT_SERVICES    *gBS;
EFI_RUNTIME_SERVICES *gRT;

VOID
UefiInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  gImageHandle = ImageHandle;
  gST          = SystemTable;
  gBS          = SystemTable->BootServices;
  gRT          = SystemTable->RuntimeServices;
}

VOID *
GetProtocol (
  IN EFI_HANDLE Handle OPTIONAL,
  IN EFI_GUID  *Guid
)
{
  VOID *Interface = NULL;

  if (!Handle || EFI_ERROR (gBS->OpenProtocol (
                   Handle, Guid, &Interface, gImageHandle, NULL,
                   EFI_OPEN_PROTOCOL_GET_PROTOCOL
                 ))) {
    return NULL;
  }
  return Interface;
}

VOID *
LocateProtocol (IN EFI_GUID *Guid)
{
  VOID *Interface = NULL;

  if (EFI_ERROR (gBS->LocateProtocol (Guid, NULL, &Interface))) {
    return NULL;
  }
  return Interface;
}

UINTN
LocateHandles (
  IN EFI_GUID     *Guid,
  OUT EFI_HANDLE **Out
)
{
  UINTN Count = 0;

  *Out = NULL;
  if (EFI_ERROR (
        gBS->LocateHandleBuffer (ByProtocol, Guid, NULL, &Count, Out)
      )) {
    return 0;
  }
  return Count;
}

BOOLEAN
CompareGuid (
  IN CONST EFI_GUID *Guid1,
  IN CONST EFI_GUID *Guid2
)
{
  return CompareMem (Guid1, Guid2, sizeof (EFI_GUID)) == 0;
}

UINT8 *
MemoryMapGet (
  OUT UINTN *Count,
  OUT UINTN *DescriptorSize
)
{
  UINTN  Size    = 0;
  UINTN  MapKey  = 0;
  UINT32 Version = 0;
  UINT8 *Map;

  *Count = *DescriptorSize = 0;
  gBS->GetMemoryMap (&Size, NULL, &MapKey, DescriptorSize, &Version);
  if (*DescriptorSize < sizeof (EFI_MEMORY_DESCRIPTOR)) {
    return NULL;
  }
  // Room for the descriptors the allocation adds.
  Size += 8 * *DescriptorSize;
  Map = AllocatePool (((Size + 7) / 8) * 8);
  if (!Map) {
    return NULL;
  }
  if (EFI_ERROR (gBS->GetMemoryMap (
        &Size, (EFI_MEMORY_DESCRIPTOR *)Map, &MapKey, DescriptorSize, &Version
      ))) {
    FreePool (Map);
    return NULL;
  }
  *Count = Size / *DescriptorSize;
  return Map;
}

// GUIDs, by their edk2 names. An edk2 module gets these from AutoGen.c.

EFI_GUID gEfiBlockIoProtocolGuid    = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid                    = EFI_FILE_INFO_ID;
EFI_GUID gEfiFileSystemVolumeLabelInfoIdGuid = EFI_FILE_SYSTEM_VOLUME_LABEL_ID;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid    = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid =
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

// Values from the .dec files.
EFI_GUID gChargerExProtocolGuid = {
  0x6edc8a6d,
  0x2663,
  0x43cd,
  {0x90, 0xff, 0x46, 0x21, 0xff, 0xd1, 0x0d, 0xf5}};
EFI_GUID gEfiDisplayPowerStateProtocolGuid = {
  0xf352021d,
  0x9593,
  0x4432,
  {0xbf, 0x04, 0x67, 0xb9, 0xf3, 0xb7, 0x60, 0x08}};
EFI_GUID gEfiDxeServicesTableGuid = {
  0x05AD34BA,
  0x6F02,
  0x4214,
  {0x95, 0x2E, 0x4D, 0xA0, 0x39, 0x8E, 0x2B, 0xB9}};
EFI_GUID gEfiHobListGuid = {0x7739F24C,
                            0x93D7,
                            0x11D4,
                            {0x9A, 0x3A, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};
EFI_GUID gEfiCpuArchProtocolGuid = {
  0x26BACCB1,
  0x6F42,
  0x11D4,
  {0xBC, 0xE7, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}};
EFI_GUID gEfiEventReadyToBootGuid = {
  0x7CE88FB3,
  0x4BD7,
  0x4679,
  {0x87, 0xA8, 0xA8, 0xD8, 0xDE, 0xE5, 0x0D, 0x2B}};
EFI_GUID gEfiEndOfDxeEventGroupGuid =
  {0x2ce967a, 0xdd7e, 0x4ffc, {0x9e, 0xe7, 0x81, 0xc, 0xf0, 0x47, 0x8, 0x80}};
EFI_GUID gOpenAblMenuHostProtocolGuid = {
  0x6b3c9a52,
  0x1f4e,
  0x4d2a,
  {0x9c, 0x61, 0x2e, 0x7f, 0x0a, 0x5d, 0x83, 0xb4}};

CONST CHAR8 *
StatusText (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[STATUS_TEXT_SIZE]
)
{
  // A warning is not "ok".
  if (Status == EFI_SUCCESS) {
    SPrint (Out, STATUS_TEXT_SIZE, "ok");
    return Out;
  }
  SPrint (
    Out, STATUS_TEXT_SIZE, "%s%u", EFI_ERROR (Status) ? "error " : "warning ",
    (UINT32)(Status & 0xFFFF)
  );
  return Out;
}

// Reads the virtual counter and its frequency in Hz. Both can be read at EL1
// and EL2.
STATIC VOID
Counter (
  OUT UINT64 *Count,
  OUT UINT64 *Frequency
)
{
  UINT64 Ticks;
  UINT64 Hz;

  __asm__ volatile ("isb\n\tmrs %0, cntvct_el0" : "=r"(Ticks));
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(Hz));
  *Count     = Ticks;
  *Frequency = Hz;
}

UINT64
ClockNowMs (VOID)
{
  UINT64 Count;
  UINT64 Frequency;

  Counter (&Count, &Frequency);
  return Frequency < 1000 ? 0 : Count / (Frequency / 1000);
}

UINT64
ClockNowUs (VOID)
{
  UINT64 Count;
  UINT64 Frequency;

  Counter (&Count, &Frequency);
  if (Frequency == 0) {
    return 0;
  }
  // Count * 1000000 / Frequency without 128-bit math, which would need a
  // compiler runtime. CNTFRQ_EL0 is 32 bits, so the remainder times 1000000
  // fits in 64.
  return Count / Frequency * 1000000 + Count % Frequency * 1000000 / Frequency;
}

VOID
InventoryConnectAll (VOID)
{
  EFI_HANDLE *Handles = NULL;
  // Empty driver list: just the NULL terminator.
  EFI_HANDLE NoDrivers[1] = {NULL};
  UINTN      Count        = 0;

  if (gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles) !=
      EFI_SUCCESS) {
    return;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    (VOID)gBS->ConnectController (Handles[Index], NoDrivers, NULL, TRUE);
  }
  gBS->FreePool (Handles);
}
