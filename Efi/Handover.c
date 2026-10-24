/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Handover - what the system needs just before Linux starts (Efi/Handover.h).
*/

#include "Efi/Handover.h"

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include <Guid/HobList.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"

// XBL memory regions by uefiplat.cfg label ("Kernel", "Display Reserved",
// ...). They come from the UEFI Config Lib, loaded through XBL's
// shared-library loader, which a GUID HOB points to. The ABL does the same,
// so nothing is hard-coded per SoC.

// XBL's shared-library loader HOB.
STATIC EFI_GUID mShlibHob = {0xbedaeabc,
                             0x5e70,
                             0x4d66,
                             {0x97, 0x33, 0x21, 0x3d, 0x07, 0x2b, 0x9d, 0x04}};

// These layouts must match XBL's.
typedef struct _SHLIB_LOADER {
  UINT32 LoaderVersion;
  VOID  *InstallLib;
  EFI_STATUS (*LoadLib) (
    IN CONST CHAR8 *Name,
    IN UINT32       Flags,
    OUT VOID      **Lib
  );
} SHLIB_LOADER;

typedef struct _MEM_REGION_INFO {
  UINT8  Name[32];
  UINT64 Base;
  UINT64 Size;
  UINT32 BuildHobOption;
  UINT32 ResourceType;
  UINT32 ResourceAttribute;
  UINT32 MemoryType;
  UINT32 CacheAttributes;
} MEM_REGION_INFO;

typedef struct _UEFI_CFG_LIB {
  UINT32 LibVersion;
  EFI_STATUS (*GetMemInfoByName) (
    IN CONST CHAR8      *Name,
    OUT MEM_REGION_INFO *Info
  );
} UEFI_CFG_LIB;

_Static_assert (
  sizeof (MEM_REGION_INFO) == 72,
  "MEM_REGION_INFO: XBL's layout"
);
_Static_assert (
  offsetof (
    SHLIB_LOADER,
    LoadLib
  ) == 16,
  "SHLIB_LOADER: XBL's layout"
);
_Static_assert (
  offsetof (
    UEFI_CFG_LIB,
    GetMemInfoByName
  ) == 8,
  "UEFI_CFG_LIB: XBL's layout"
);

STATIC UEFI_CFG_LIB *mLibFound;
STATIC BOOLEAN       mTried;

// Finds the HOB list XBL passed to DXE in the configuration tables.
STATIC CONST UINT8 *
HobList (VOID)
{
  for (UINTN Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    EFI_CONFIGURATION_TABLE *Table = &gST->ConfigurationTable[Index];
    if (CompareGuid (&Table->VendorGuid, &gEfiHobListGuid)) {
      return Table->VendorTable;
    }
  }
  return NULL;
}

// Returns the data of the first GUID extension HOB for Guid, or NULL.
STATIC CONST UINT8 *
GuidHob (IN CONST EFI_GUID *Guid)
{
  CONST UINT8 *Hob = HobList ();

  if (!Hob) {
    return NULL;
  }
  for (INT32 Index = 0; Index < 65536; Index++) {
    UINT16 Type   = ReadLe16 (Hob);
    UINTN  Length = ReadLe16 (Hob + 2);

    if (Type == EFI_HOB_TYPE_END_OF_HOB_LIST || Length < 8) {
      return NULL;
    }
    if (Type == EFI_HOB_TYPE_GUID_EXTENSION &&
        CompareGuid ((CONST EFI_GUID *)(Hob + 8), Guid)) {
      return Hob + 24;
    }
    Hob += Length;
  }
  return NULL;
}

// Loads the UEFI Config Lib on the first call. Returns NULL if XBL has none.
STATIC UEFI_CFG_LIB *
ConfigLib (VOID)
{
  if (!mTried) {
    CONST UINT8 *Data;

    mTried = TRUE;
    // Same call as the ABL's. The library stays loaded.
    Data = GuidHob (&mShlibHob);
    if (Data) {
      CONST SHLIB_LOADER *Loader  = *(CONST SHLIB_LOADER * CONST *)Data;
      VOID               *Library = NULL;

      if (Loader && Loader->LoadLib &&
          Loader->LoadLib ("UEFI Config Lib", 0, &Library) == EFI_SUCCESS &&
          Library) {
        mLibFound = Library;
      }
    }
  }
  return mLibFound;
}

// Looks up XBL's region Name, truncated to 63 bytes. Base and Size must be
// page-aligned and non-zero. Returns FALSE if XBL has no such region.
STATIC BOOLEAN
XblConfigRegion (
  IN CONST CHAR8 *Name,
  OUT UINT64     *Base,
  OUT UINT64     *Size
)
{
  UEFI_CFG_LIB   *Library   = ConfigLib ();
  MEM_REGION_INFO Info      = {0};
  CHAR8           CName[64] = {0};
  CONST UINT64    PageMask  = 0xFFF;
  UINTN           Length;

  if (!Library || !Library->GetMemInfoByName) {
    return FALSE;
  }
  Length = AsciiStrLen (Name);
  if (Length > sizeof (CName) - 1) {
    Length = sizeof (CName) - 1;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    CName[Index] = Name[Index];
  }
  if (Library->GetMemInfoByName (CName, &Info) != EFI_SUCCESS || !Info.Base ||
      !Info.Size || Info.Base & PageMask || Info.Size & PageMask) {
    return FALSE;
  }
  *Base = Info.Base;
  *Size = Info.Size;
  return TRUE;
}

// Hypervisor call that stops it logging to the UART.
#define HYP_DISABLE_UART_LOGGING 0x86000000ull

VOID
HandoverHypUartOff (VOID)
{
#if defined(__aarch64__)
  // SMCCC fast call. It may clobber x0-x17 and nothing else.
  register UINT64 X0 __asm__ ("x0") = HYP_DISABLE_UART_LOGGING;
  register UINT64 X1 __asm__ ("x1") = 0;
  register UINT64 X2 __asm__ ("x2") = 0;
  register UINT64 X3 __asm__ ("x3") = 0;
  register UINT64 X4 __asm__ ("x4") = 0;
  register UINT64 X5 __asm__ ("x5") = 0;
  register UINT64 X6 __asm__ ("x6") = 0;
  register UINT64 X7 __asm__ ("x7") = 0;

  __asm__ volatile ("smc #0"
                    : "+r"(X0), "+r"(X1), "+r"(X2), "+r"(X3), "+r"(X4),
                      "+r"(X5), "+r"(X6), "+r"(X7)
                    :
                    : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                      "x16", "x17", "memory");
#endif
}

BOOLEAN
HandoverDisplayRegion (
  OUT UINT64 *Base,
  OUT UINT64 *Len
)
{
  return XblConfigRegion ("Display Reserved", Base, Len) ||
         XblConfigRegion ("Display_Reserved", Base, Len);
}
