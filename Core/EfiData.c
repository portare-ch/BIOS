/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  EfiData - UEFI boot manager data formats as plain bytes.
*/

#include "Core/EfiData.h"

#include "Base/Memory.h"
#include "Base/Print.h"

CONST UINT8 gUefiEndNode[4] = {UEFI_TYPE_END, UEFI_END_ENTIRE, 4, 0};

BOOLEAN
UefiNextNode (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN OUT UINTN   *At,
  OUT UEFI_NODE  *Out
)
{
  UINTN Offset = *At;
  UINTN Len;

  if (!InBounds (Size, Offset, 4)) {
    return FALSE;
  }
  Len = ReadLe16 (DevicePath + Offset + 2);
  if (DevicePath[Offset] == UEFI_TYPE_END || Len < 4 ||
      !InBounds (Size, Offset, Len)) {
    return FALSE;
  }
  *Out = (UEFI_NODE){
    DevicePath[Offset], DevicePath[Offset + 1], {DevicePath + Offset, Len}};
  *At = Offset + Len;
  return TRUE;
}

BOOLEAN
UefiPartGuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  OUT UINT8       Guid[16]
)
{
  UEFI_NODE  Node;
  OABL_BYTES Hd = {0};

  for (UINTN At = 0; UefiNextNode (DevicePath, Size, &At, &Node);) {
    if (Node.Type == UEFI_TYPE_MEDIA && Node.SubType == UEFI_MEDIA_HARD_DRIVE) {
      Hd = UefiNodeData (&Node);
    }
  }
  // PartitionNumber(4) Start(8) Size(8) Signature(16) MBRType(1)
  // SignatureType(1), 2 for a GUID.
  if (Hd.Len < 38 || Hd.Ptr[37] != 0x02) {
    return FALSE;
  }
  CopyMem (Guid, Hd.Ptr + 20, 16);
  return TRUE;
}

BOOLEAN
UefiPartUuid (
  IN CONST UINT8 *DevicePath,
  IN UINTN        Size,
  IN BOOLEAN      Upper,
  OUT CHAR8       Out[UEFI_PART_UUID_SIZE]
)
{
  UINT8 Guid[16];

  Out[0] = 0;
  if (!UefiPartGuid (DevicePath, Size, Guid)) {
    return FALSE;
  }
  SPrint (
    Out, UEFI_PART_UUID_SIZE,
    Upper ? "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"
          : "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    (UINT32)ReadLe32 (Guid), (UINT32)ReadLe16 (Guid + 4),
    (UINT32)ReadLe16 (Guid + 6), Guid[8], Guid[9], Guid[10], Guid[11], Guid[12],
    Guid[13], Guid[14], Guid[15]
  );
  return TRUE;
}

BOOLEAN
UefiPaddedDtb (
  IN CONST UINT8  *Blob,
  IN UINTN         Size,
  IN UINTN         Pad,
  OUT OABL_BUFFER *Out
)
{
  UINT32 Total;

  *Out = (OABL_BUFFER){0};
  if (!GetBe32 (Blob, Size, 4, &Total)) {
    return FALSE;
  }
  if (ReadBe32 (Blob) != 0xd00dfeed || Total > Size) {
    return FALSE;
  }
  // The copy must hold the totalsize field and must not overflow.
  if (Pad > MAX_UINTN - Total || Total + Pad < 8) {
    return FALSE;
  }
  if (!BufferReserve (Out, Total + Pad) || !BufferAppend (Out, Blob, Total) ||
      !BufferAppendZeros (Out, Pad)) {
    BufferFree (Out);
    return FALSE;
  }
  WriteBe32 (Out->Data + 4, (UINT32)(Total + Pad));
  return TRUE;
}
