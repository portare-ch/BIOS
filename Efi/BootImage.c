/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  BootImage - finds and starts PortareOS's boot image (Efi/BootImage.h).
*/

#include "Efi/BootImage.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/AndroidImage.h"
#include "Core/DeviceTree.h"
#include "Core/EfiData.h"
#include "Efi/Abl.h"
#include "Efi/Files.h"
#include "Efi/Handover.h"
#include "Efi/Quirks.h"

// sizeof (boot_img_hdr) for a version 0 header.
#define HEADER 1632
#define PAGE   4096

#define STATUS_STR(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

#define SAY(Log, ...) LogPrint ((Log), "boot image: " __VA_ARGS__)

// Names the partition a boot image was loaded from, as PARTUUID=<guid>, so
// PortareOS's initramfs mounts that installation: an SD card written from
// an image and an install made from it carry the same labels.
#define BOOT_PART_KEY "portare.bootpart=PARTUUID="

BOOLEAN
BootImageIsOn (IN EFI_HANDLE Fs)
{
  OABL_BUFFER Head = {0};
  BOOLEAN     Ok;

  Ok = FilesReadPart (
         Fs, BOOT_IMAGE_PATH, 0, sizeof (ANDROID_BOOT_MAGIC) - 1, &Head
       ) &&
       Head.Len >= sizeof (ANDROID_BOOT_MAGIC) - 1 &&
       CompareMem (
         Head.Data, ANDROID_BOOT_MAGIC, sizeof (ANDROID_BOOT_MAGIC) - 1
       ) == 0;
  BufferFree (&Head);
  return Ok;
}

// Allocates zeroed pool for Size bytes, rounded up to whole pages.
STATIC UINT8 *
Pool (IN UINTN Size)
{
  UINTN Rounded = (Size + PAGE - 1) / PAGE * PAGE;
  VOID *Buffer;

  if (gBS->AllocatePool (EfiBootServicesData, Rounded, &Buffer) !=
      EFI_SUCCESS) {
    return NULL;
  }
  ZeroMem (Buffer, Rounded);
  return Buffer;
}

STATIC BOOLEAN
IsBootImage (
  IN CONST UINT8 *Buffer,
  IN UINTN        Size
)
{
  return Size >= HEADER && CompareMem (Buffer, ANDROID_BOOT_MAGIC, 8) == 0;
}

// Reads the boot image straight off FAT32 into pool. Returns NULL unless it
// is a boot image of Want bytes (any size if Want is MAX_UINTN).
STATIC UINT8 *
ReadRaw (
  IN EFI_HANDLE Fs,
  IN UINTN      Want,
  OUT UINTN    *Size
)
{
  OABL_BUFFER Data  = {0};
  UINT8      *Image = NULL;

  if (!RawFatReadRange (Fs, BOOT_IMAGE_PATH, 0, MAX_UINTN, &Data)) {
    BufferFree (&Data);
    return NULL;
  }
  if ((Want == MAX_UINTN || Want == Data.Len) &&
      IsBootImage (Data.Data, Data.Len) && (Image = Pool (Data.Len))) {
    CopyMem (Image, Data.Data, Data.Len);
    *Size = Data.Len;
  }
  BufferFree (&Data);
  return Image;
}

// Reads the boot image on Fs through the file system.
STATIC UINT8 *
FromFileSystem (
  IN EFI_HANDLE Fs,
  IN LOG_SINK   Log,
  OUT UINTN    *Size
)
{
  CONST CHAR8 *Path = BOOT_IMAGE_PATH;
  UINT64       Bytes;
  UINT8       *Image;
  UINT8       *RawImage;
  UINTN        Got;

  if (!BootImageIsOn (Fs) || !FilesSize (Fs, Path, &Bytes)) {
    return NULL;
  }
  if (Bytes < HEADER || Bytes > MAX_UINT32) {
    SAY (Log, "%s: %llu bytes, not a boot image", Path, (UINT64)Bytes);
    return NULL;
  }
  if (!(Image = Pool ((UINTN)Bytes))) {
    return NULL;
  }
  if (!FilesReadTo (Fs, Path, Image, (UINTN)Bytes, &Got) || Got != Bytes) {
    // XBL's FAT driver came up short. Try reading FAT32 directly.
    gBS->FreePool (Image);
    SAY (Log, "%s: the read came up short, trying straight off FAT32", Path);
    RawImage = ReadRaw (Fs, (UINTN)Bytes, Size);
    if (!RawImage) {
      SAY (Log, "%s: no luck straight off FAT32 either", Path);
    }
    return RawImage;
  }
  if (CompareMem (Image, ANDROID_BOOT_MAGIC, 8) != 0) {
    gBS->FreePool (Image);
    SAY (Log, "%s: not an Android boot image", Path);
    return NULL;
  }
  SAY (Log, "%s: %llu bytes", Path, (UINT64)Bytes);
  *Size = (UINTN)Bytes;
  return Image;
}

// Describes Image, found on Drive.
STATIC BOOT_IMAGE_FOUND
FoundOn (
  IN EFI_HANDLE Drive,
  IN UINT8     *Image,
  IN UINTN      Size
)
{
  BOOT_IMAGE_FOUND Found = {.Buffer = Image, .Size = Size};
  OABL_BUFFER Path = {0};

  if (PathsHandlePath (Drive, &Path)) {
    UefiPartUuid (Path.Data, Path.Len, FALSE, Found.PartUuid);
  }
  BufferFree (&Path);
  return Found;
}

BOOLEAN
BootImageFind (
  IN CONST EFI_HANDLE  *Drives,
  IN UINTN              Count,
  IN LOG_SINK           Log,
  OUT BOOT_IMAGE_FOUND *Out
)
{
  UINT8 *Image;
  UINTN  Size;

  for (UINTN Index = 0; Index < Count; Index++) {
    if ((Image = FromFileSystem (Drives[Index], Log, &Size))) {
      *Out = FoundOn (Drives[Index], Image, Size);
      return TRUE;
    }
  }
  // XBL's FAT driver may not see the file at all.
  for (UINTN Index = 0; Index < Count; Index++) {
    if ((Image = ReadRaw (Drives[Index], MAX_UINTN, &Size))) {
      SAY (
        Log, "%s: found straight off FAT32, %llu bytes", BOOT_IMAGE_PATH,
        (UINT64)Size
      );
      *Out = FoundOn (Drives[Index], Image, Size);
      return TRUE;
    }
  }
  return FALSE;
}

// Starting the image

// Where BootLinux puts a gzip kernel in XBL's kernel region.
#define KERNEL_64BIT_LOAD_OFFSET 0x80000ull
// Room BootLinux leaves for the devicetree (DT_SIZE_2MB).
#define DT_SIZE ((UINT64)2 << 20)
// Padding for our devicetree fix-ups. The ABL's own update grows it further.
#define DTB_PAD 8192
// boot_img_hdr.cmdline: BOOT_ARGS_SIZE bytes at offset 64. It is the only
// command line BootLinux takes from a v0-v2 image.
#define CMDLINE_AT  64
#define CMDLINE_END 576

STATIC EFI_GUID mQcomTokenSpace = {
  0x882f8c2b,
  0x9646,
  0x435f,
  {0x8d, 0xe5, 0xf2, 0x08, 0xff, 0x80, 0xc1, 0xbd}};

STATIC UINT64
RoundUp (
  IN UINT64 Value,
  IN UINT64 Alignment
)
{
  return (Value + Alignment - 1) / Alignment * Alignment;
}

// Reads the little-endian UINT64 at Offset, or 0 past the end.
STATIC UINT64
U64At (
  IN CONST UINT8 *Buffer,
  IN UINTN        Size,
  IN UINTN        Offset
)
{
  UINT64 Value;

  return GetLe64 (Buffer, Size, Offset, &Value) ? Value : 0;
}

// Reads a UINT64 variable from Qualcomm's token space. Shorter ones read 0.
STATIC BOOLEAN
QcomU64 (
  IN CHAR16  *Name,
  OUT UINT64 *Out
)
{
  UINT8 Bytes[8] = {0};
  UINTN Size     = sizeof (Bytes);

  if (gRT->GetVariable (Name, &mQcomTokenSpace, NULL, &Size, Bytes) !=
      EFI_SUCCESS) {
    return FALSE;
  }
  *Out = U64At (Bytes, Size, 0);
  return TRUE;
}

// Gets XBL's kernel region, where BootLinux lays out the kernel.
STATIC BOOLEAN
KernelRegion (
  OUT UINT64 *Base,
  OUT UINT64 *Size
)
{
  return QcomU64 (L"KernelBaseAddr", Base) && QcomU64 (L"KernelSize", Size);
}

// Copies the Nova's devicetree (BOOT_IMAGE_MODEL) out of the Size bytes of
// DTBs at Dtbs into Out, padded. Returns "" on success, else the reason.
STATIC CONST CHAR8 *
NovaDtb (
  IN CONST UINT8  *Dtbs,
  IN UINTN         Size,
  OUT OABL_BUFFER *Out
)
{
  ANDROID_DTBS Found;
  CONST UINT8 *Blob     = NULL;
  UINTN        BlobSize = 0;

  AndroidFindDtbs (Dtbs, Size, &Found);
  for (UINTN Index = 0; Index < Found.Count && !Blob; Index++) {
    DT_INFO Info;

    if (!DtInspect (
          Dtbs + Found.Items[Index].Offset, Found.Items[Index].Size, &Info
        )) {
      continue;
    }
    if (AsciiStrEq (Info.Model, BOOT_IMAGE_MODEL)) {
      Blob     = Dtbs + Found.Items[Index].Offset;
      BlobSize = Found.Items[Index].Size;
    }
    DtInfoFree (&Info);
  }
  VecFree (&Found);
  if (!Blob) {
    return "no devicetree for the " BOOT_IMAGE_MODEL " in it";
  }
  if (!UefiPaddedDtb (Blob, BlobSize, DTB_PAD, Out)) {
    return "its devicetree doesn't parse";
  }
  return "";
}

// Lays out the image and starts it. Returns the reason if it fails.
STATIC CONST CHAR8 *
LayOut (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN LOG_SINK                Log
)
{
  CONST UINT8       *BootImage = Found->Buffer;
  ANDROID_BOOT_IMAGE Header;
  ANDROID_SECTION    KernelSection;
  ANDROID_SECTION    RamdiskSection;
  OABL_BUFFER        Dtb     = {0};
  OABL_BUFFER        Cmdline = {0};
  CONST UINT8       *Kernel, *Ramdisk, *Image, *BaseCmdline;
  UINTN              KernelSize;
  UINTN              RamdiskSize;
  UINTN              Written;
  UINTN              Appended;
  UINTN              Room;
  UINTN              BaseCmdlineLength;
  UINT64             Page;
  UINT64             RegionBase;
  UINT64             RegionSize;
  UINT64             RegionEnd;
  UINT64             RamdiskAddr;
  UINT64             DtbAddr;
  UINT64             TextOffset;
  UINT64             KernelAddr;
  UINT64             ImageSize;
  UINT32             Version;
  CONST CHAR8       *Why;
  UINT8             *Out;
  BOOLEAN            Gzip;
  BOOLEAN            Ok;
  OPENABL_KERNEL     Layout;
  EFI_STATUS         Status;

  if (!AndroidBootImageParse (BootImage, Found->Size, &Header)) {
    return "not an Android boot image";
  }
  Version        = Header.HeaderVersion;
  Page           = Header.PageSize;
  KernelSection  = Header.Kernel;
  RamdiskSection = Header.Ramdisk;
  AndroidBootImageFree (&Header);
  if (Version >= 3) {
    return "a header v3 or later image needs vendor_boot: not started";
  }
  // An empty section may have any offset.
  if (KernelSection.Size &&
      !InBounds (Found->Size, KernelSection.Offset, KernelSection.Size)) {
    return "the kernel runs past the image's end";
  }
  if (RamdiskSection.Size &&
      !InBounds (Found->Size, RamdiskSection.Offset, RamdiskSection.Size)) {
    return "the ramdisk runs past the image's end";
  }
  Kernel = KernelSection.Size ? BootImage + KernelSection.Offset : BootImage;
  KernelSize = KernelSection.Size;
  Ramdisk = RamdiskSection.Size ? BootImage + RamdiskSection.Offset : BootImage;
  RamdiskSize = RamdiskSection.Size;
  if (!KernelRegion (&RegionBase, &RegionSize)) {
    return "XBL didn't say where the kernel goes (KernelBaseAddr, KernelSize)";
  }

  // BootLinux's layout: the ramdisk a page short of the end of the region,
  // the devicetree 2 MiB and a page below it, the kernel from the start.
  // The first test keeps the address arithmetic from wrapping.
  if (RamdiskSize > RegionSize ||
      RoundUp (RamdiskSize, Page) + Page + DT_SIZE + Page > RegionSize) {
    return "no room left for the kernel";
  }
  RegionEnd   = RegionBase + RegionSize;
  RamdiskAddr = RegionEnd - (RoundUp (RamdiskSize, Page) + Page);
  DtbAddr     = RamdiskAddr - (DT_SIZE + Page);
  Gzip        = KernelSize >= 2 && Kernel[0] == 0x1f && Kernel[1] == 0x8b;
  TextOffset  = U64At (Kernel, KernelSize, 8);
  KernelAddr  = RegionBase + (Gzip || U64At (Kernel, KernelSize, 16) == 0
                                ? KERNEL_64BIT_LOAD_OFFSET
                                : TextOffset);
  if (DtbAddr <= KernelAddr) {
    return "no room left for the kernel";
  }
  Room = (UINTN)(DtbAddr - KernelAddr);
  // Written straight into XBL's kernel region, as BootLinux does.
  Out = (UINT8 *)(UINTN)KernelAddr;
  if (Gzip) {
    if (AblLibInflate (Kernel, KernelSize, Out, Room, &Written, &Appended) !=
        EFI_SUCCESS) {
      return "inflating the kernel failed";
    }
  } else {
    if (KernelSize > Room) {
      return "the kernel is larger than its room";
    }
    CopyMem (Out, Kernel, KernelSize);
    Written  = KernelSize;
    Appended = 0;
  }
  Image = Out;
  if (Written < 60 || CompareMem (Image + 56, "ARM\x64", 4) != 0) {
    return "not an arm64 kernel Image";
  }
  ImageSize = U64At (Image, Written, 16);
  if (ImageSize > Room) {
    return "the kernel's runtime size reaches the devicetree";
  }

  // Pick the Nova's devicetree from those appended to the kernel.
  if (Appended > KernelSize) {
    Appended = KernelSize;
  }
  Why = NovaDtb (Kernel + Appended, KernelSize - Appended, &Dtb);
  if (*Why) {
    BufferFree (&Dtb);
    return Why;
  }
  if (Dtb.Len > DT_SIZE) {
    BufferFree (&Dtb);
    return "its devicetree is larger than 2 MiB";
  }
  QuirksFixupDeviceTree (Dtb.Data, Dtb.Len, Log);

  // The image's command line, then where it came from.
  BaseCmdline       = BootImage + CMDLINE_AT;
  BaseCmdlineLength = Found->Size >= CMDLINE_END ? CMDLINE_END - CMDLINE_AT : 0;
  {
    UINTN Length = 0;

    while (Length < BaseCmdlineLength && BaseCmdline[Length]) {
      Length++;
    }
    BaseCmdlineLength = Length < BaseCmdlineLength
                          ? Length
                          : (BaseCmdlineLength < 511 ? BaseCmdlineLength : 511);
  }
  Ok = BufferFrom (&Cmdline, BaseCmdline, BaseCmdlineLength);
  if (Ok && Found->PartUuid[0]) {
    Ok = (!Cmdline.Len || Cmdline.Data[Cmdline.Len - 1] == ' ' ||
          BufferAppendU8 (&Cmdline, ' ')) &&
         BufferAppend (&Cmdline, BOOT_PART_KEY, sizeof (BOOT_PART_KEY) - 1) &&
         BufferAppend (
           &Cmdline, Found->PartUuid, AsciiStrLen (Found->PartUuid)
         );
    SAY (Log, "booted from partition %s", Found->PartUuid);
  }
  if (!Ok || !BufferAppendU8 (&Cmdline, 0)) {
    BufferFree (&Cmdline);
    BufferFree (&Dtb);
    return "out of memory for the command line";
  }

  CopyMem ((VOID *)(UINTN)DtbAddr, Dtb.Data, Dtb.Len);
  CopyMem ((VOID *)(UINTN)RamdiskAddr, Ramdisk, RamdiskSize);
  SAY (
    Log,
    "kernel %llu bytes at 0x%llx, devicetree at 0x%llx, ramdisk %llu bytes "
    "at 0x%llx, command line %llu bytes",
    (UINT64)Written, (UINT64)KernelAddr, (UINT64)DtbAddr,
    (UINT64)RamdiskSize, (UINT64)RamdiskAddr, (UINT64)(Cmdline.Len - 1)
  );

  // Stop the hypervisor logging to the UART the kernel takes over.
  HandoverHypUartOff ();
  Layout = (OPENABL_KERNEL){
    .KernelAddr  = KernelAddr,
    .KernelSize  = RoundUp (ImageSize != 0 ? ImageSize : Written, Page),
    .RamdiskAddr = RamdiskAddr,
    .RamdiskSize = RamdiskSize,
    .DtbAddr     = DtbAddr,
    .CmdLine     = (CONST CHAR8 *)Cmdline.Data,
  };
  Status = MenuHostStartKernel (Host, &Layout);
  SAY (Log, "the ABL couldn't start the kernel (%s)", STATUS_STR (Status));
  BufferFree (&Cmdline);
  BufferFree (&Dtb);
  return "not started";
}

VOID
BootImageStart (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN LOG_SINK                Log
)
{
  CONST CHAR8 *Why;
  EFI_STATUS   Status;

  Status = MenuHostAction (Host, OPENABL_MENU_ACTION_BOOT_IMAGE_VERIFY);
  if (Status != EFI_SUCCESS) {
    SAY (Log, "verified boot refused it (%s)", STATUS_STR (Status));
    return;
  }
  // Once verified, the buffer is ours again.
  Why = LayOut (Host, Found, Log);
  gBS->FreePool (Found->Buffer);
  SAY (Log, "%s", Why);
}

EFI_STATUS
BootImageStartVerified (
  IN CONST MENU_HOST        *Host,
  IN CONST BOOT_IMAGE_FOUND *Found,
  IN LOG_SINK                Log
)
{
  SAY (Log, "%s", LayOut (Host, Found, Log));
  return EFI_LOAD_ERROR;
}
