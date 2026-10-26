/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

#include "Core/LinuxBoot.h"
#include "Base/Text.h"

LINUX_DISTRO
LinuxDistroOf (
  IN CONST CHAR8 *Label OPTIONAL,
  IN BOOLEAN      ArmadaStamp
)
{
  if (ArmadaStamp || (Label && AsciiStrEq (Label, "ARMADA"))) {
    return LINUX_DISTRO_ARMADAOS;
  }
  if (Label && AsciiStrEq (Label, "PORTAREOS")) {
    return LINUX_DISTRO_PORTAREOS;
  }
  if (Label && AsciiStrEq (Label, "ROCKNIX")) {
    return LINUX_DISTRO_ROCKNIX;
  }
  return LINUX_DISTRO_UNKNOWN;
}

CONST CHAR8 *
LinuxDistroName (IN LINUX_DISTRO Distro)
{
  switch (Distro) {
  case LINUX_DISTRO_PORTAREOS:
    return "PortareOS";
  case LINUX_DISTRO_ARMADAOS:
    return "ArmadaOS";
  case LINUX_DISTRO_ROCKNIX:
    return "ROCKNIX";
  default:
    return "Linux";
  }
}

BOOLEAN
LinuxBootCmdline (
  IN LINUX_DISTRO  Distro,
  IN CONST CHAR8  *ImageCmdline,
  IN CONST CHAR8  *PartUuid,
  OUT OABL_BUFFER *Out
)
{
  STATIC CONST CHAR8 Key[] = "portare.bootpart=PARTUUID=";
  BOOLEAN            Ok;

  Ok = BufferFrom (Out, ImageCmdline, AsciiStrLen (ImageCmdline));
  if (Ok && Distro == LINUX_DISTRO_PORTAREOS && PartUuid[0]) {
    Ok = (!Out->Len || Out->Data[Out->Len - 1] == ' ' ||
          BufferAppendU8 (Out, ' ')) &&
         BufferAppend (Out, Key, sizeof (Key) - 1) &&
         BufferAppend (Out, PartUuid, AsciiStrLen (PartUuid));
  }
  if (!Ok || !BufferAppendU8 (Out, 0)) {
    BufferFree (Out);
    return FALSE;
  }
  return TRUE;
}
