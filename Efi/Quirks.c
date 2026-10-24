/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Quirks - live patches for bugs in XBL's SD card driver, and the storage
  protocol the early copy of the menu leaves installed for the ABL.
*/

#include "Efi/Quirks.h"

#include "Efi/Efi.h"

#include <Guid/EventGroup.h>
#include <Protocol/Cpu.h>

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Efi/Abl.h"
#include "Efi/BootImage.h"
#include "Efi/Dxe.h"
#include "Efi/MenuHost.h"
#include "Efi/Splash.h"
#include "Efi/Variables.h"

// Event group on which XBL's SdccDxe re-detects the SD card.
STATIC EFI_GUID mDetectSdCard = {
  0xb7972c36,
  0x8a4c,
  0x4a56,
  {0x8b, 0x02, 0x11, 0x59, 0xb5, 0x2d, 0x4b, 0xfb}};

STATIC EFI_GUID mStorageGuid = OPENABL_STORAGE_PROTOCOL_GUID;

// The code signatures are stored XORed with KEY. The scans search the
// firmware's code and loaded images, which include copies of this image, so
// plain signatures would match themselves. HIDE() applies the key at compile
// time; Reveal() undoes it through a volatile the optimiser can't fold.
#define KEY     0x5au
#define HIDE(x) ((UINT8)((x) ^ KEY))

STATIC VOID
Reveal (
  IN CONST UINT8 *Hidden,
  OUT UINT8      *Out,
  IN UINTN        Count
)
{
  volatile UINT8 Key = KEY;

  for (UINTN Index = 0; Index < Count; Index++) {
    Out[Index] = (UINT8)(Hidden[Index] ^ Key);
  }
}

// Bytes whose Mask is 0 match anything.
STATIC BOOLEAN
Matches (
  IN CONST UINT8 *Haystack,
  IN UINTN        HaystackLen,
  IN UINTN        Offset,
  IN CONST UINT8 *Sig,
  IN CONST UINT8 *Mask,
  IN UINTN        Length
)
{
  if (Offset > HaystackLen || Length > HaystackLen - Offset) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    if (Mask[Index] != 0 && Haystack[Offset + Index] != Sig[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

// Unaligned reads from a mapped image.

// Patching driver code

#define Q_EFI_MEMORY_WB 0x8ull
#define Q_EFI_MEMORY_RP 0x2000ull
#define Q_EFI_MEMORY_RO 0x20000ull
#define PAGE            4096ull

// Clears the page's write protection. TRUE only if the GCD then reports it
// writable. The CPU call must ask for write-back: XBL ignores any other
// request but still returns EFI_SUCCESS.
STATIC BOOLEAN
MakeWritable (IN UINT64 Addr)
{
  UINT64                          Page = Addr & ~(PAGE - 1);
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor;
  EFI_CPU_ARCH_PROTOCOL          *Cpu;

  if (!DxeDescriptor (Page, &Descriptor)) {
    return FALSE;
  }
  DxeSetAttributes (
    Page, PAGE, Descriptor.Attributes & ~(Q_EFI_MEMORY_RO | Q_EFI_MEMORY_RP)
  );
  Cpu = LocateProtocol (&gEfiCpuArchProtocolGuid);
  if (Cpu) {
    Cpu->SetMemoryAttributes (Cpu, Page, PAGE, Q_EFI_MEMORY_WB);
  }
  return DxeDescriptor (Page, &Descriptor) &&
         (Descriptor.Attributes & Q_EFI_MEMORY_RO) == 0;
}

// Makes the new instruction visible to instruction fetch.
STATIC VOID
SyncCode (IN UINT64 Addr)
{
#if defined(__aarch64__)
  __asm__ volatile ("dc civac, %0\n\t"
                    "dsb ish\n\t"
                    "ic ivau, %0\n\t"
                    "dsb ish\n\t"
                    "isb\n\t"
                    :
                    : "r"(Addr)
                    : "memory");
#else
  (VOID)Addr;
#endif
}

// Replaces the word at Addr if it is Orig (any word when !HasOrig). TRUE if
// it reads back as NewWord.
STATIC BOOLEAN
WriteWord (
  IN UINT64  Addr,
  IN BOOLEAN HasOrig,
  IN UINT32  Orig,
  IN UINT32  NewWord
)
{
  volatile UINT32 *Word = (volatile UINT32 *)(UINTN)Addr;

  if ((HasOrig && *Word != Orig) || !MakeWritable (Addr)) {
    return FALSE;
  }
  *Word = NewWord;
  SyncCode (Addr);
  return *Word == NewWord;
}

// Firmware code regions

typedef struct _CODE_REGION {
  UINT64 Start, Len;
} CODE_REGION;
typedef struct _CODE_REGIONS OABL_VEC (CODE_REGION) CODE_REGIONS;

// Returns the boot services, runtime services and loader code regions from
// the memory map. Caller frees the result with VecFree.
STATIC CODE_REGIONS
CodeRegions (VOID)
{
  CODE_REGIONS Out = {0};
  UINTN        Count;
  UINTN        DescriptorSize;
  UINT8       *Map = MemoryMapGet (&Count, &DescriptorSize);

  for (UINTN Index = 0; Map && Index < Count; Index++) {
    CONST EFI_MEMORY_DESCRIPTOR *Descriptor =
      MEMORY_MAP_AT (Map, DescriptorSize, Index);

    if (Descriptor->Type == EfiBootServicesCode ||
        Descriptor->Type == EfiRuntimeServicesCode ||
        Descriptor->Type == EfiLoaderCode) {
      if (!VecPush (
            &Out, ((CODE_REGION){Descriptor->PhysicalStart,
                                 Descriptor->NumberOfPages * PAGE})
          )) {
        break;
      }
    }
  }
  FreePool (Map);
  return Out;
}

STATIC BOOLEAN
ImageRange (
  IN EFI_HANDLE Handle,
  OUT UINT64   *Base,
  OUT UINT64   *Size
)
{
  EFI_LOADED_IMAGE_PROTOCOL *Image =
    GetProtocol (Handle, &gEfiLoadedImageProtocolGuid);

  if (!Image || !Image->ImageBase || Image->ImageSize == 0) {
    return FALSE;
  }
  *Base = (UINT64)(UINTN)Image->ImageBase;
  *Size = Image->ImageSize;
  return TRUE;
}

STATIC BOOLEAN
Inside (
  IN UINT64  Addr,
  IN BOOLEAN Have,
  IN UINT64  Base,
  IN UINT64  Size
)
{
  return Have && Addr >= Base && Addr < Base + Size;
}

STATIC VOID EFIAPI
Nothing (
  IN EFI_EVENT Event,
  IN VOID     *Context
)
{
  (VOID)Event;
  (VOID)Context;
}

// Signals an event group, as the ABL's SignalGuidEvent does.
STATIC VOID
SignalGroup (IN EFI_GUID *Group)
{
  EFI_EVENT Event;

  if (!EFI_ERROR (gBS->CreateEventEx (
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Nothing, NULL, Group, &Event
      ))) {
    gBS->SignalEvent (Event);
    gBS->CloseEvent (Event);
  }
}

VOID
QuirksSignalSdDetect (VOID)
{
  SignalGroup (&mDetectSdCard);
}

// SdccDxe: the 1.8 V request

// The movk that sets S18R in the ACMD41 argument and what follows it, in
// the SdccDxe builds. Stored XORed (HIDE).
STATIC CONST UINT8 mAcmd41[2][24] = {
  {HIDE (0x15), HIDE (0x00), HIDE (0x90), HIDE (0x52), HIDE (0xf5),
   HIDE (0x3f), HIDE (0xa8), HIDE (0x72), HIDE (0x00), HIDE (0x00),
   HIDE (0x00), HIDE (0x00), HIDE (0xf7), HIDE (0x06), HIDE (0x80),
   HIDE (0x52), HIDE (0x00), HIDE (0x00), HIDE (0x00), HIDE (0x00),
   HIDE (0x39), HIDE (0x05), HIDE (0x80), HIDE (0x52)},
  {HIDE (0x15), HIDE (0x00), HIDE (0x90), HIDE (0x52), HIDE (0xf5),
   HIDE (0x3f), HIDE (0xa8), HIDE (0x72), HIDE (0x00), HIDE (0x00),
   HIDE (0x00), HIDE (0x00), HIDE (0xf6), HIDE (0x03), HIDE (0x1f),
   HIDE (0x2a), HIDE (0x37), HIDE (0x00), HIDE (0x80), HIDE (0x52),
   HIDE (0x38), HIDE (0x05), HIDE (0x80), HIDE (0x52)},
};
STATIC CONST UINT8 mAcmd41Mask[2][24] = {
  {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
  {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};
#define MOVK_AT         4
#define MOVK_S18R_SET   0x72a83ff5u
#define MOVK_S18R_CLEAR 0x72a81ff5u
#define MAX_SITES 16

typedef struct _U64_LIST
OABL_VEC (UINT64) U64_LIST;

// Finds every ACMD41 site in firmware code outside this image and returns
// the movk addresses. Caller frees the result with VecFree.
STATIC U64_LIST
Acmd41Sites (VOID)
{
  UINT8        Sig0[24];
  UINT8        Sig1[24];
  U64_LIST     Sites   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);

  Reveal (mAcmd41[0], Sig0, sizeof (Sig0));
  Reveal (mAcmd41[1], Sig1, sizeof (Sig1));
  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64       Start = Regions.Items[Index].Start;
    UINT64       Len   = Regions.Items[Index].Len;
    CONST UINT8 *Code  = (CONST UINT8 *)(UINTN)Start;

    // Instructions are word-aligned; unaligned matches are data.
    for (UINTN Offset = 0; Offset + 4 <= Len; Offset += 4) {
      if (Code[Offset] != Sig0[0]) {
        continue;
      }
      if ((Matches (Code, Len, Offset, Sig0, mAcmd41Mask[0], sizeof (Sig0)) ||
           Matches (Code, Len, Offset, Sig1, mAcmd41Mask[1], sizeof (Sig1))) &&
          !Inside (Start + Offset, HaveOwn, OwnBase, OwnSize)) {
        if (!VecPush (&Sites, Start + Offset + MOVK_AT)) {
          break;
        }
      }
    }
  }
  VecFree (&Regions);
  return Sites;
}

// SdccDxe: the card's power-up time

// Every SdccDxe build tries ACMD41 200 times, 5 ms apart, so a card has a
// second to power up, and one still busy then is never found.
#define HIDE32(x)    ((UINT32)((x) ^ 0x5a5a5a5au))
#define SETUP_WORDS  16
#define LOOP_WORDS   32
#define ANY_REGISTER 0x1fu
STATIC CONST UINT32 mCmd8Check = HIDE32 (0x7106a91fu);
STATIC CONST UINT32 mWait5Ms   = HIDE32 (0x52827114u);
STATIC CONST UINT32 mAcmd41Index = HIDE32 (0x52800520u);
STATIC CONST UINT32 mCountDown[2] = {HIDE32 (0x52801916u),
                                     HIDE32 (0x52806416u)};
STATIC CONST UINT32 mCountUp[2][2] = {
  {HIDE32 (0x710322dfu), HIDE32 (0x710c82dfu)},
  {HIDE32 (0x71031edfu), HIDE32 (0x710c7edfu)},
};

STATIC UINT32
Reveal32 (IN UINT32 Hidden)
{
  volatile UINT32 Key = HIDE32 (0);

  return Hidden ^ Key;
}

typedef struct _TRIES_WORD {
  UINT64  At;
  UINT32  Built;
  UINT32  Longer;
  BOOLEAN Patched;
} TRIES_WORD;
typedef struct _TRIES_WORDS
OABL_VEC (TRIES_WORD) TRIES_WORDS;

// Returns the offset of the first of Count words of Code from Offset, ending
// before End, that is Want once the Ignore bits are cleared, or End if none
// is.
STATIC UINTN
FindWord (
  IN CONST UINT32 *Code,
  IN UINTN         End,
  IN UINTN         Offset,
  IN UINTN         Count,
  IN UINT32        Want,
  IN UINT32        Ignore
)
{
  for (UINTN Index = 0; Index < Count && Offset + 4 <= End;
       Index++, Offset += 4) {
    if ((Code[Offset / 4] & ~Ignore) == Want) {
      return Offset;
    }
  }
  return End;
}

// Finds every ACMD41 power up limit in firmware code outside this image.
// Caller frees the result with VecFree.
STATIC TRIES_WORDS
TriesWords (VOID)
{
  TRIES_WORDS  Words   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);
  UINT32       Cmd8    = Reveal32 (mCmd8Check);
  UINT32       Wait    = Reveal32 (mWait5Ms);
  UINT32       Acmd41  = Reveal32 (mAcmd41Index);

  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64        Start = Regions.Items[Index].Start;
    UINTN         Len   = (UINTN)Regions.Items[Index].Len;
    CONST UINT32 *Code  = (CONST UINT32 *)(UINTN)Start;

    for (UINTN Offset = 0; Offset + 4 <= Len; Offset += 4) {
      TRIES_WORD Found = {0};
      UINTN      Delay;
      UINTN      At;

      if (Code[Offset / 4] != Cmd8 ||
          Inside (Start + Offset, HaveOwn, OwnBase, OwnSize)) {
        continue;
      }
      Delay = FindWord (Code, Len, Offset, SETUP_WORDS, Wait, 0);
      if (Delay == Len || FindWord (
                            Code, Delay, Offset, SETUP_WORDS, Acmd41,
                            ANY_REGISTER
                          ) == Delay) {
        continue;
      }

      At = FindWord (
        Code, Delay, Offset, SETUP_WORDS, Reveal32 (mCountDown[0]), 0
      );
      if (At != Delay) {
        Found = (TRIES_WORD){Start + At, Reveal32 (mCountDown[0]),
                             Reveal32 (mCountDown[1]), FALSE};
      }
      for (UINTN Form = 0; Form < 2 && !Found.At; Form++) {
        At = FindWord (
          Code, Len, Delay, LOOP_WORDS, Reveal32 (mCountUp[Form][0]), 0
        );
        if (At != Len) {
          Found = (TRIES_WORD){Start + At, Reveal32 (mCountUp[Form][0]),
                               Reveal32 (mCountUp[Form][1]), FALSE};
        }
      }
      if (Found.At && !VecPush (&Words, Found)) {
        break;
      }
    }
  }
  VecFree (&Regions);
  return Words;
}

// SdccDxe: the bus speed at 3.3 V
#define SPEED_WORDS 5
STATIC CONST UINT32 mSpeedSelect[SPEED_WORDS] = {
  HIDE32 (0xb9400298u), HIDE32 (0x7100171fu), HIDE32 (0x54000001u),
  HIDE32 (0x52800119u), HIDE32 (0x52800076u)};
STATIC CONST UINT32 mSpeedSelectIgnore[SPEED_WORDS] = {0, 0, 0x00ffffe0u, 0,
                                                       0};
STATIC CONST UINT32 mHighSpeed = HIDE32 (0x52800058u);

// Finds every speed selection in firmware code outside this image and
// returns the addresses of its loads. Caller frees the result with VecFree.
STATIC U64_LIST
SpeedSelects (VOID)
{
  U64_LIST     Sites   = {0};
  CODE_REGIONS Regions = CodeRegions ();
  UINT64       OwnBase = 0;
  UINT64       OwnSize = 0;
  BOOLEAN      HaveOwn = ImageRange (gImageHandle, &OwnBase, &OwnSize);
  UINT32       Want[SPEED_WORDS];

  for (UINTN Word = 0; Word < SPEED_WORDS; Word++) {
    Want[Word] = Reveal32 (mSpeedSelect[Word]);
  }
  for (UINTN Index = 0; Index < Regions.Count; Index++) {
    UINT64        Start = Regions.Items[Index].Start;
    UINTN         Len   = (UINTN)Regions.Items[Index].Len;
    CONST UINT32 *Code  = (CONST UINT32 *)(UINTN)Start;

    for (UINTN Offset = 0; Offset + 4 * SPEED_WORDS <= Len; Offset += 4) {
      UINTN Word = 0;

      while (Word < SPEED_WORDS &&
             (Code[Offset / 4 + Word] & ~mSpeedSelectIgnore[Word]) ==
               Want[Word]) {
        Word++;
      }
      if (Word == SPEED_WORDS &&
          !Inside (Start + Offset, HaveOwn, OwnBase, OwnSize) &&
          !VecPush (&Sites, Start + Offset)) {
        break;
      }
    }
  }
  VecFree (&Regions);
  return Sites;
}

UINT32
QuirksSdccUhs (IN LOG_SINK Log)
{
  U64_LIST    Sites   = Acmd41Sites ();
  TRIES_WORDS Tries   = TriesWords ();
  UINT32      Patched = 0;
  UINTN       Longer  = 0;

  if (Sites.Count > MAX_SITES) {
    Sites.Count = MAX_SITES;
  }
  if (Tries.Count > MAX_SITES) {
    Tries.Count = MAX_SITES;
  }
  for (UINTN Index = 0; Index < Sites.Count; Index++) {
    UINT64       Site    = Sites.Items[Index];
    BOOLEAN Ok = WriteWord (Site, TRUE, MOVK_S18R_SET, MOVK_S18R_CLEAR);

    LogPrint (
      Log, "sdcc: site %llu @0x%llx -> %s", (UINT64)Index, (UINT64)Site,
      Ok ? "patched" : "REFUSED"
    );
    Patched += Ok ? 1 : 0;
  }
  if (Sites.Count == 0) {
    LogPrint (
      Log,
      "sdcc: no ACMD41 site found, signature does not match this firmware build"
    );
  }
  for (UINTN Index = 0; Index < Tries.Count; Index++) {
    TRIES_WORD *Word = &Tries.Items[Index];

    Word->Patched = WriteWord (Word->At, TRUE, Word->Built, Word->Longer);
    LogPrint (
      Log, "sdcc: ACMD41 tries 200 -> 800 @0x%llx: %s", (UINT64)Word->At,
      Word->Patched ? "patched" : "REFUSED"
    );
    Longer += Word->Patched ? 1 : 0;
  }
  if (Tries.Count == 0) {
    LogPrint (Log, "sdcc: no ACMD41 power-up limit found");
  }
  // Only with S18R clear do cards stay at 3.3 V.
  if (Patched > 0) {
    U64_LIST Speeds = SpeedSelects ();

    if (Speeds.Count > MAX_SITES) {
      Speeds.Count = MAX_SITES;
    }
    for (UINTN Index = 0; Index < Speeds.Count; Index++) {
      BOOLEAN Ok = WriteWord (
        Speeds.Items[Index], TRUE, Reveal32 (mSpeedSelect[0]),
        Reveal32 (mHighSpeed)
      );

      LogPrint (
        Log, "sdcc: bus speed @0x%llx: the slot's -> high speed: %s",
        (UINT64)Speeds.Items[Index], Ok ? "patched" : "REFUSED"
      );
    }
    if (Speeds.Count == 0) {
      LogPrint (Log, "sdcc: no bus speed selection found");
    }
    VecFree (&Speeds);
  }
  if (Patched > 0 || Longer > 0) {
    // Detection runs inside SignalEvent, as its notify TPL is above ours.
    QuirksSignalSdDetect ();
  }
  for (UINTN Index = 0; Index < Tries.Count; Index++) {
    CONST TRIES_WORD *Word = &Tries.Items[Index];
    BOOLEAN           Ok;

    if (!Word->Patched) {
      continue;
    }
    Ok = WriteWord (Word->At, TRUE, Word->Longer, Word->Built);
    LogPrint (
      Log, "sdcc: ACMD41 tries back to 200 @0x%llx: %s", (UINT64)Word->At,
      Ok ? "done" : "REFUSED"
    );
  }
  VecFree (&Tries);
  VecFree (&Sites);
  return Patched;
}

VOID
QuirksPrepareStorage (VOID)
{
  QuirksSignalSdDetect ();
  InventoryConnectAll ();
}

// The storage protocol

// Entry point for Run, or NULL.
STATIC OPENABL_STORAGE_RUN mRunFn;

VOID
QuirksSetRun (IN OPENABL_STORAGE_RUN Run)
{
  mRunFn = Run;
}

VOID
QuirksToAbl (
  IN OPENABL_STORAGE_LOG Log OPTIONAL,
  IN CONST CHAR8        *Line
)
{
  CHAR8 *Prefixed;

  if (!Log) {
    return;
  }
  Prefixed = SPrintAlloc ("openabl-menu: %s", Line);
  if (Prefixed) {
    Log ((CONST CHAR8 *)Prefixed);
    FreePool (Prefixed);
  }
}

// LOG_SINK callback. Context carries the ABL's log function.
STATIC VOID
AblLogSink (
  IN VOID        *Context OPTIONAL,
  IN CONST CHAR8 *Line
)
{
  QuirksToAbl ((OPENABL_STORAGE_LOG)(UINTN)Context, Line);
}

LOG_SINK
QuirksAblLog (IN OPENABL_STORAGE_LOG Log OPTIONAL)
{
  return (LOG_SINK){AblLogSink, (VOID *)(UINTN)Log};
}

// Only AYANEO's and KONKR's panels need androidboot ids, so there are none.
STATIC EFI_STATUS EFIAPI
PAndroidParams (
  IN OPENABL_STORAGE_PROTOCOL *This,
  OUT CHAR8                   *Out OPTIONAL,
  IN OUT UINTN                *Size,
  IN OPENABL_STORAGE_LOG       Log OPTIONAL
)
{
  (VOID)This, (VOID)Out, (VOID)Size, (VOID)Log;
  return EFI_NOT_FOUND;
}

VOID
QuirksFixupDeviceTree (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
)
{
  SplashReserveSplash (Fdt, Cap, Log);
}

STATIC EFI_STATUS EFIAPI
PStartBootImage (
  IN OPENABL_STORAGE_PROTOCOL *This,
  IN VOID                     *Buffer,
  IN UINTN                     Size,
  IN OPENABL_STORAGE_LOG       Log
)
{
  MENU_HOST        Host;
  BOOT_IMAGE_FOUND Found;

  (VOID)This;
  if (!Buffer) {
    return EFI_INVALID_PARAMETER;
  }
  // Fastboot runs inside the menu's boot, so the host is installed.
  if (!MenuHostLocate (&Host)) {
    return EFI_UNSUPPORTED;
  }
  // Only while PortareOS starts first; with Android first, Qualcomm's
  // BootLinux starts it.
  if (VariablesAndroidFirst ()) {
    QuirksToAbl (Log, "boot image: Android starts first: Qualcomm's boot");
    return EFI_UNSUPPORTED;
  }
  Found = (BOOT_IMAGE_FOUND){.Buffer = Buffer, .Size = Size};
  return BootImageStartVerified (&Host, &Found, QuirksAblLog (Log));
}

STATIC VOID EFIAPI
PBootPrep (
  IN OPENABL_STORAGE_PROTOCOL *This,
  IN UINT32                    Flags,
  IN OPENABL_STORAGE_LOG       Log OPTIONAL
)
{
  STATIC BOOLEAN Prepared;
  LOG_SINK       Sink = QuirksAblLog (Log);

  (VOID)This;
  if (Flags & OPENABL_PREP_BOOT && !Prepared) {
    Prepared = TRUE;
    SignalGroup (&gEfiEventReadyToBootGuid);
    SignalGroup (&gEfiEndOfDxeEventGroupGuid);
    LogLine (Sink, "boot: ReadyToBoot and EndOfDxe signalled");
  }
  QuirksPrepareStorage ();
}

EFI_STATUS
QuirksInstallProtocol (VOID)
{
  OPENABL_STORAGE_PROTOCOL *Protocol;
  EFI_HANDLE                Handle = NULL;

  Protocol = AllocatePool (sizeof (*Protocol));
  if (!Protocol) {
    return EFI_OUT_OF_RESOURCES;
  }
  Protocol->AndroidParams  = PAndroidParams;
  Protocol->BootPrep       = PBootPrep;
  Protocol->Run            = mRunFn;
  Protocol->StartBootImage = PStartBootImage;
  // The ABL's debug output has nowhere to go.
  Protocol->LogSink        = NULL;
  // Never freed: the interface lives as long as the menu.
  return gBS->InstallProtocolInterface (
    &Handle, &mStorageGuid, EFI_NATIVE_INTERFACE, Protocol
  );
}
