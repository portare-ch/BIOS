/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Main - entry point of the menu program.

  The ABL starts us with its menu-host protocol installed: FLAG_EARLY
  patches XBL's drivers and stays loaded, FLAG_MAIN runs the whole boot.
*/

#include "Menu/Menu.h"

#include "Base/Memory.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Core/BootDecision.h"
#include "Efi/Abl.h"
#include "Efi/BootImage.h"
#include "Efi/Efi.h"
#include "Efi/Files.h"
#include "Efi/Quirks.h"
#include "Efi/Variables.h"

MENU_HOST gMenuHost;
BOOLEAN   gMenuHostSet;
BOOLEAN   gFastbootUp;

VOID
MenuLog (
  IN CONST CHAR8 *Fmt,
  ...
)
{
  OABL_STRING Line = {0};
  va_list     Args;

  if (!gMenuHostSet) {
    return;
  }
  StringAppend (&Line, "openabl-menu: ");
  va_start (Args, Fmt);
  StringVPrint (&Line, Fmt, Args);
  va_end (Args);
  MenuHostLog (&gMenuHost, StringGet (&Line));
  StringFree (&Line);
}

STATIC VOID
LogToSink (
  IN VOID        *Context OPTIONAL,
  IN CONST CHAR8 *Line
)
{
  (VOID)Context;
  MenuLog ("%s", Line);
}

CONST LOG_SINK gMenuEfiLog = {LogToSink, NULL};

CONST CHAR8 *
MenuStatus (
  IN EFI_STATUS Status,
  OUT CHAR8     Out[MENU_STATUS_SIZE]
)
{
  SPrint (
    Out, MENU_STATUS_SIZE, "%s%llu", EFI_ERROR (Status) ? "error " : "",
    (UINT64)(Status & 0xffff)
  );
  return Out;
}

// Booting

VOID
MenuBootVolumes (
  OUT EFI_HANDLE **Volumes,
  OUT UINTN       *Count
)
{
  VOLUMES_HANDLE_LIST Handles = {0};

  VolumesBoot (&Handles);
  *Volumes = Handles.Items;
  *Count   = Handles.Count;
}

// Finds the first boot image on Volumes and hands it to the ABL to start.
// Returns FALSE if there is none or the ABL refused it.
STATIC BOOLEAN
HandOverBootImageFrom (
  IN CONST MENU_HOST   *Host,
  IN CONST EFI_HANDLE  *Volumes,
  IN UINTN              Count,
  OUT BOOT_IMAGE_FOUND *Image
)
{
  EFI_STATUS Status;

  if (!BootImageFind (Volumes, Count, gMenuEfiLog, Image)) {
    MenuLog ("no boot image");
    return FALSE;
  }
  Status = MenuHostSetBootImage (Host, Image->Buffer, Image->Size);
  if (Status != EFI_SUCCESS) {
    MenuLog ("handing the ABL the boot image: %s", MENU_STATUS (Status));
    // The ABL didn't take BootImageFind's buffer, so free it here.
    gBS->FreePool (Image->Buffer);
    return FALSE;
  }
  return TRUE;
}

typedef enum {
  PICK_NONE,    // nothing was picked
  PICK_HANDED,  // its boot image went to the ABL
  PICK_GONE,    // its drive is gone; nothing else may start instead
  PICK_FAILED,  // it has no boot image; nothing else may start instead
} PICK_OUTCOME;

// Hands the ABL the boot image of the drive the boot list picked, and
// clears the pick.
STATIC PICK_OUTCOME
BootPick (
  IN CONST MENU_HOST   *Host,
  OUT BOOT_IMAGE_FOUND *Image
)
{
  VOLUMES_HANDLE_LIST Volumes = {0};
  OABL_BUFFER         Pick    = {0};
  OABL_BUFFER         Path    = {0};
  EFI_HANDLE          Drive   = NULL;

  if (!MenuVarGet (MENU_PICK_VAR, &Pick)) {
    return PICK_NONE;
  }
  MenuVarPut (MENU_PICK_VAR, NULL, 0);
  MenuFlush ();
  VolumesBoot (&Volumes);
  for (UINTN Index = 0; Index < Volumes.Count && !Drive; Index++) {
    if (PathsHandlePath (Volumes.Items[Index], &Path) &&
        Path.Len == Pick.Len &&
        CompareMem (Path.Data, Pick.Data, Pick.Len) == 0) {
      Drive = Volumes.Items[Index];
    }
    BufferFree (&Path);
  }
  BufferFree (&Pick);
  MenuLog ("the boot list's pick: %s", Drive ? "its drive" : "no drive");
  if (!Drive) {
    VecFree (&Volumes);
    return PICK_GONE;
  }
  VecFree (&Volumes);
  return HandOverBootImageFrom (Host, &Drive, 1, Image) ? PICK_HANDED
                                                        : PICK_FAILED;
}

// Starts Linux: the boot list's pick, or else the first boot image on
// internal storage, then the SD card, through Qualcomm's verified boot.
// Returns only if nothing started, with a MENU_START_RESULT_* reason.
STATIC UINT8
LinuxBoot (IN CONST MENU_HOST *Host)
{
  BOOT_IMAGE_FOUND Image;
  EFI_HANDLE      *Volumes;
  UINTN            Count;
  BOOLEAN          Handed;

  MenuHostAction (Host, OPENABL_MENU_ACTION_BOOT_PREP);
  switch (BootPick (Host, &Image)) {
  case PICK_GONE:
    // After an explicit choice, never another system.
    return MENU_START_RESULT_PICK_GONE;
  case PICK_FAILED:
    return MENU_START_RESULT_PICK_FAILED;
  case PICK_HANDED:
    Handed = TRUE;
    break;
  case PICK_NONE:
  default:
    MenuBootVolumes (&Volumes, &Count);
    Handed = HandOverBootImageFrom (Host, Volumes, Count, &Image);
    FreePool (Volumes);
    break;
  }
  if (!Handed) {
    return MENU_START_RESULT_NOTHING;
  }
  BootImageStart (Host, &Image, gMenuEfiLog);
  return MENU_START_RESULT_FAILED;
}

// Starts fastboot and runs the menu, which serves fastboot from its poll,
// until fastboot ends. Qualcomm's menu is shown if ours can't run. If
// starting PortareOS from the menu fails, the menu comes back and says why.
STATIC EFI_STATUS
FastbootPath (
  IN CONST MENU_HOST *Host,
  IN UINT8            CameBack
)
{
  EFI_STATUS Status = MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_START);

  if (Status != EFI_SUCCESS) {
    MenuLog ("fastboot didn't start (%s)", MENU_STATUS (Status));
    return Status;
  }
  gFastbootUp = TRUE;
  for (;;) {
    MENU_END End;

    End = MenuRun (Host, CameBack);
    if (End.Kind == MENU_END_LEFT) {
      break;
    }
    if (End.Kind == MENU_END_FAILED) {
      MenuLog (
        "the menu couldn't run (%s): Qualcomm's fastboot menu",
        MENU_STATUS (End.Status)
      );
      MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_MENU);
      break;
    }
    MenuLog ("starting PortareOS in this boot");
    CameBack = LinuxBoot (Host);
    MenuLog ("nothing booted (%u)", CameBack);
  }
  gFastbootUp = FALSE;
  return MenuHostAction (Host, OPENABL_MENU_ACTION_FASTBOOT_STOP);
}

// Takes the system the menu picked for this boot (MENU_ONCE_*), or 0 for
// none, and clears it, so a key held now can't leave it for a later boot.
STATIC UINT8
TargetOnce (VOID)
{
  OABL_BUFFER Once   = {0};
  UINT8       Target = 0;

  if (!MenuVarGet (MENU_ONCE_VAR, &Once)) {
    return 0;
  }
  MenuVarPut (MENU_ONCE_VAR, NULL, 0);
  MenuFlush ();
  // A damaged value picks nothing.
  if (Once.Len == 1 &&
      (Once.Data[0] == MENU_ONCE_LINUX || Once.Data[0] == MENU_ONCE_ANDROID)) {
    Target = Once.Data[0];
  }
  BufferFree (&Once);
  MenuLog (
    "the menu picked %s for this boot", Target == MENU_ONCE_ANDROID ? "Android"
                                        : Target                    ? "PortareOS"
                                                                    : "nothing"
  );
  return Target;
}

// Runs the boot described at MainBoot.
STATIC EFI_STATUS
RunBoot (
  IN CONST MENU_HOST       *Host,
  IN CONST MENU_HOST_STATE *State
)
{
  BOOT_DECISION_START Start = MenuHostStateStart (State);
  UINT8               Once  = TargetOnce ();
  BOOLEAN             Charger;

  if (State->Entry & OPENABL_MENU_ENTRY_FASTBOOT) {
    MenuLog ("fastboot was asked for");
    return FastbootPath (Host, MENU_START_RESULT_NONE);
  }
  if (BootDecisionFastboot (&Start)) {
    MenuLog ("fastboot was asked for (Delete held)");
    return FastbootPath (Host, MENU_START_RESULT_NONE);
  }
  // Qualcomm's charger check, if the charger screen is enabled.
  Charger =
    State->OffmodeCharging != 0 &&
    MenuHostAction (Host, OPENABL_MENU_ACTION_CHARGER_BOOT) == EFI_SUCCESS;
  // BIOS shows its own charging screen, with or without Android, unless
  // the menu picked something to start.
  if (Charger && !Once) {
    MenuLog ("powered on by the charger: the charging screen");
    return FastbootPath (Host, MENU_START_RESULT_CHARGING);
  }
  // The menu's pick wins over the Boot order and Volume Up. Only the keys
  // for fastboot, above, still apply.
  if (Once ? Once == MENU_ONCE_ANDROID : State->BootMode != 0) {
    BOOLEAN Recovery = BootDecisionRecovery (
      &Start, Once == MENU_ONCE_ANDROID || VariablesAndroidFirst ()
    );
    EFI_STATUS Status;

    MenuLog ("%s", Recovery ? "booting Android's recovery" : "booting Android");
    Status = MenuHostAction (
      Host, Recovery ? OPENABL_MENU_ACTION_BOOT_ANDROID_RECOVERY
                     : OPENABL_MENU_ACTION_BOOT_ANDROID
    );
    MenuLog ("Android didn't boot (%s)", MENU_STATUS (Status));
    return FastbootPath (Host, MENU_START_RESULT_ANDROID_FAILED);
  }
  return FastbootPath (Host, LinuxBoot (Host));
}

// FLAG_MAIN: runs the whole boot. Fastboot if a key or the reboot reason
// asks for it, the charging screen when the charger switched the Nova on,
// else Android or its recovery if the Boot order or Volume Up says so, else
// PortareOS, falling back to the menu. Returns EFI_UNSUPPORTED, having done
// nothing, if the ABL's state can't be read.
STATIC EFI_STATUS
MainBoot (IN CONST MENU_HOST *Host)
{
  MENU_HOST_STATE State;
  EFI_STATUS      Status;

  if (EFI_ERROR (MenuHostState (Host, &State))) {
    return EFI_UNSUPPORTED;
  }
  // RunBoot always acts, and EFI_UNSUPPORTED would make the ABL boot as if
  // we hadn't run.
  Status = RunBoot (Host, &State);
  return Status == EFI_UNSUPPORTED ? EFI_ABORTED : Status;
}

// Returns TRUE if we are a boot-service driver. Unlike an application, it
// stays loaded after returning EFI_SUCCESS, so the storage protocol can
// remain.
STATIC BOOLEAN
Resident (VOID)
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage =
    GetProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid);

  return LoadedImage && LoadedImage->ImageCodeType == EfiBootServicesCode;
}

STATIC EFI_STATUS EFIAPI
RunHere (IN OPENABL_STORAGE_PROTOCOL *This);

// FLAG_EARLY: patches out SdccDxe's 1.8 V request, then stays loaded with
// the storage protocol the ABL connects its storage through.
STATIC EFI_STATUS
Early (IN CONST MENU_HOST *Host)
{
  UINT32     Patched = QuirksSdccUhs (gMenuEfiLog);
  EFI_STATUS Status;

  (VOID)Host;
  MenuLog ("early: %u SdccDxe site(s) patched", Patched);
  if (!Resident ()) {
    MenuLog (
      "built as an application, so it can't stay loaded: no storage protocol"
    );
    Status = EFI_UNSUPPORTED;
  } else {
    // From now on the ABL runs this copy through the storage protocol.
    QuirksSetRun (RunHere);
    Status = QuirksInstallProtocol ();
    if (EFI_ERROR (Status)) {
      MenuLog (
        "offering the storage protocol failed: %s", MENU_STATUS (Status)
      );
    }
  }
  // Run XBL's SD card detection once more as the ABL goes on.
  QuirksSignalSdDetect ();
  return Status;
}

// Entry

// Does what the host's flags ask. Again is TRUE when the ABL runs the copy
// left loaded at its start.
STATIC EFI_STATUS
Run (
  IN CONST MENU_HOST *Host,
  IN BOOLEAN          Again
)
{
  CHAR8 Hello[96];

  gMenuHost    = *Host;
  gMenuHostSet = TRUE;
  // Set up the ABL's libfdt and inflate before anything edits a devicetree.
  AblLibInit ();
  SPrint (
    Hello, sizeof (Hello), "openabl-menu %s%s", MENU_VERSION,
    Again ? " running again (the copy loaded at the start)" : " starting"
  );
  if (MenuHostFlags (Host) & OPENABL_MENU_FLAG_EARLY) {
    MenuHostLog (Host, Hello);
    return Early (Host);
  }
  MenuHostLog (Host, Hello);
  if (!(MenuHostFlags (Host) & OPENABL_MENU_FLAG_MAIN)) {
    return EFI_UNSUPPORTED;
  }
  return MainBoot (Host);
}

// The storage protocol's Run. The ABL calls it to run the copy left loaded
// at the early start instead of loading another.
STATIC EFI_STATUS EFIAPI
RunHere (IN OPENABL_STORAGE_PROTOCOL *This)
{
  MENU_HOST Host;

  (VOID)This;
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  if (!MenuHostLocate (&Host)) {
    return EFI_NOT_FOUND;
  }
  return Run (&Host, TRUE);
}

EFI_STATUS EFIAPI
efi_main (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  MENU_HOST Host;

  UefiInit (ImageHandle, SystemTable);
  gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  // Started by something other than the ABL.
  if (!MenuHostLocate (&Host)) {
    return EFI_NOT_FOUND;
  }
  return Run (&Host, FALSE);
}
