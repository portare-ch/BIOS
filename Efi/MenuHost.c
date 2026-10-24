/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  MenuHost - the interface between the ABL and the menu it embeds.
*/

#include "Efi/MenuHost.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Efi/Abl.h"
#include "Efi/Variables.h"


CONST CHAR8 *
MenuHostActionName (IN UINT32 Action)
{
  switch (Action) {
  case OPENABL_MENU_ACTION_START:
    return "START";
  case OPENABL_MENU_ACTION_RESTART_BOOTLOADER:
    return "RESTART_BOOTLOADER";
  case OPENABL_MENU_ACTION_RECOVERY:
    return "RECOVERY";
  case OPENABL_MENU_ACTION_EMERGENCY:
    return "EMERGENCY";
  case OPENABL_MENU_ACTION_POWER_OFF:
    return "POWER_OFF";
  case OPENABL_MENU_ACTION_ALTERNATE_SLOT:
    return "ALTERNATE_SLOT";
  case OPENABL_MENU_ACTION_FASTBOOT_START:
    return "FASTBOOT_START";
  case OPENABL_MENU_ACTION_FASTBOOT_STOP:
    return "FASTBOOT_STOP";
  case OPENABL_MENU_ACTION_FASTBOOT_MENU:
    return "FASTBOOT_MENU";
  case OPENABL_MENU_ACTION_BOOT_PREP:
    return "BOOT_PREP";
  case OPENABL_MENU_ACTION_BOOT_ANDROID:
    return "BOOT_ANDROID";
  case OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY:
    return "BOOT_ANDROID_RECOVERY";
  case OPENABL_MENU_ACTION_GADGET_PAUSE:
    return "GADGET_PAUSE";
  case OPENABL_MENU_ACTION_GADGET_RESUME:
    return "GADGET_RESUME";
  case OPENABL_MENU_ACTION_BOOT_IMAGE_VERIFY:
    return "BOOT_IMAGE_VERIFY";
  case OPENABL_MENU_ACTION_CHARGER_BOOT:
    return "CHARGER_BOOT";
  case OPENABL_MENU_ACTION_CHARGER_SCREEN_ON:
    return "CHARGER_SCREEN_ON";
  case OPENABL_MENU_ACTION_CHARGER_SCREEN_OFF:
    return "CHARGER_SCREEN_OFF";
  }
  return "?";
}

BOOLEAN
MenuHostLocate (OUT MENU_HOST *Out)
{
  EFI_HANDLE *Handles;
  UINTN       Count = LocateHandles (&gOpenAblMenuHostProtocolGuid, &Handles);
  // The host keeps the protocol installed until the menu returns.
  OPENABL_MENU_HOST_PROTOCOL *Protocol =
    Count ? GetProtocol (Handles[0], &gOpenAblMenuHostProtocolGuid) : NULL;

  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!Protocol) {
    return FALSE;
  }
  Out->Protocol = Protocol;
  return TRUE;
}


EFI_STATUS
MenuHostState (
  IN CONST MENU_HOST  *Host,
  OUT MENU_HOST_STATE *Out
)
{
  OPENABL_MENU_STATE Abl    = {0};
  EFI_STATUS         Status = Host->Protocol->GetState (Host->Protocol, &Abl);

  *Out = (MENU_HOST_STATE){0};
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  Out->OffmodeCharging = Abl.OffmodeCharging;
  Out->Flags           = Abl.Flags;
  Out->Entry           = Abl.Entry;
  Out->Key             = Abl.Key;
  CopyMem (Out->Version, Abl.Version, sizeof (Out->Version));
  // Android starts first by the Boot order, or with Volume Up held.
  {
    BOOT_DECISION_START Start = MenuHostStateStart (Out);

    Out->BootMode = BootDecisionAndroid (&Start, VariablesAndroidFirst ());
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MenuHostAction (
  IN CONST MENU_HOST *Host,
  IN UINT32           Action
)
{
  return Host->Protocol->DoAction (Host->Protocol, Action);
}

EFI_STATUS
MenuHostPoll (IN CONST MENU_HOST *Host)
{
  return Host->Protocol->Poll (Host->Protocol);
}

VOID
MenuHostLog (
  IN CONST MENU_HOST *Host,
  IN CONST CHAR8     *Line
)
{
  Host->Protocol->Log (Host->Protocol, (CONST CHAR8 *)Line);
}

EFI_STATUS
MenuHostStartKernel (
  IN CONST MENU_HOST      *Host,
  IN CONST OPENABL_KERNEL *Kernel
)
{
  return Host->Protocol->StartKernel (Host->Protocol, Kernel);
}

EFI_STATUS
MenuHostSetBootImage (
  IN CONST MENU_HOST *Host,
  IN VOID            *Buffer,
  IN UINTN            Size
)
{
  return Host->Protocol->SetBootImage (Host->Protocol, Buffer, Size);
}

CHAR8 *
MenuHostText (
  IN CONST CHAR8 *Field,
  IN UINTN        Size
)
{
  UINTN Length = 0;

  while (Length < Size && Field[Length]) {
    Length++;
  }
  return Utf8Lossy ((CONST UINT8 *)Field, Length);
}

BOOT_DECISION_START
MenuHostStateStart (IN CONST MENU_HOST_STATE *State)
{
  return (BOOT_DECISION_START){
    .Key      = State->Key,
    .Fastboot = (State->Entry & OPENABL_MENU_ENTRY_FASTBOOT) != 0,
    .Recovery = (State->Entry & OPENABL_MENU_ENTRY_RECOVERY) != 0,
  };
}
