/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Menu - the menu, the Boot order it stores, and its commands.
*/

#include "Menu/Menu.h"

#include "Base/Memory.h"
#include "Base/Buffer.h"
#include "Base/Print.h"
#include "Base/Text.h"
#include "Efi/Abl.h"
#include "Efi/Battery.h"
#include "Efi/BootImage.h"
#include "Efi/Efi.h"
#include "Efi/Display.h"
#include "Efi/Files.h"
#include "Efi/Input.h"
#include "Efi/Install.h"
#include "Efi/Qcom.h"
#include "Efi/Quirks.h"
#include "Efi/Variables.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"
#include "View/View.h"

// The Nova's portrait panel (960 x 1280) is mounted sideways: the picture
// turns this far clockwise.
#define NOVA_ROTATION 90

// Variables

BOOLEAN
MenuVarGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  return VariablesGet (Name, Out);
}

EFI_STATUS
MenuVarPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  return VariablesPut (Name, Data, Size);
}

VOID
MenuFlush (VOID)
{
  EFI_STATUS Status;

  if (QcomFlushVariables (&Status) &&
      (EFI_ERROR (Status) || Status == EFI_WARN_WRITE_FAILURE)) {
    MenuLog ("FlushVariableNV: %s", MENU_STATUS (Status));
  }
}

// Whether Android is installed: gone only once Replace Android removed it,
// and assumed there when internal storage can't be read. Connects the
// storage first.
STATIC BOOLEAN
AndroidThere (VOID)
{
  GPT_SURVEY Internal;

  QuirksPrepareStorage ();
  return !InstallInternal (&Internal) || Internal.Android;
}

// The menu

typedef struct _MENU_STATE {
  MENU_HOST Host;
  BOOT_MODE BootMode;
  BOOLEAN   Android;
  // Why the last start of PortareOS didn't boot (MENU_START_RESULT_*).
  UINT8 CameBack;
  // The ABL's version, for the header.
  CHAR8 AblVersion[64];
} MENU_STATE;

// Sets up a menu run.
STATIC EFI_STATUS
MenuNew (
  OUT MENU_STATE     *M,
  IN CONST MENU_HOST *Host,
  IN UINT8            CameBack
)
{
  MENU_HOST_STATE State;
  EFI_STATUS      Status = MenuHostState (Host, &State);
  CHAR8          *Version;

  if (EFI_ERROR (Status)) {
    return Status;
  }
  *M = (MENU_STATE){
    .Host     = *Host,
    .BootMode = VariablesAndroidFirst () ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX,
    .Android  = AndroidThere (),
    .CameBack = CameBack,
  };
  Version = MenuHostText (State.Version, sizeof (State.Version));
  AsciiStrCopy (M->AblVersion, sizeof (M->AblVersion), Version ? Version : "");
  FreePool (Version);
  return EFI_SUCCESS;
}

// Stores the Boot order, flushes it and reads it back. Returns an error
// message, or NULL.
STATIC CONST CHAR8 *
SaveBootMode (
  IN OUT MENU_STATE *M,
  IN BOOT_MODE       Want
)
{
  UINT8      Byte   = Want == BOOT_MODE_ANDROID ? 1 : 0;
  EFI_STATUS Status = MenuVarPut (VARIABLES_BOOT_MODE, &Byte, 1);

  MenuFlush ();
  M->BootMode = VariablesAndroidFirst () ? BOOT_MODE_ANDROID : BOOT_MODE_LINUX;
  MenuLog ("set %s = %u (%s)", VARIABLES_BOOT_MODE, Byte, MENU_STATUS (Status));
  return EFI_ERROR (Status) || M->BootMode != Want ? gStrings->AblSettingFailed
                                                   : NULL;
}

STATIC EFI_STATUS
Act (
  IN MENU_STATE *M,
  IN UINT32      Action
)
{
  EFI_STATUS Status;

  MenuLog ("action %s", MenuHostActionName (Action));
  Status = MenuHostAction (&M->Host, Action);
  MenuLog ("%s -> %s", MenuHostActionName (Action), MENU_STATUS (Status));
  return Status;
}

// The boot list

// The drives the boot list scanned, and each row's drive (NULL for
// Android).
STATIC VOLUMES_HANDLE_LIST mDrives;
STATIC EFI_HANDLE          mEntryDrive[MAX_ENTRIES];

// The name a drive's boot image has in the boot list: PortareOS on its
// own boot partition, else Linux.
STATIC CONST CHAR8 *
BootImageTitle (IN EFI_HANDLE Fs)
{
  CHAR8  *Label     = FilesVolumeLabel (Fs);
  BOOLEAN PortareOs = Label && AsciiStrEq (Label, "PORTAREOS");

  FreePool (Label);
  return PortareOs ? gStrings->PortareOs : "Linux";
}

// Fills the boot list: each drive's boot image, internal storage first,
// then the SD card, then Android.
STATIC VOID
FillEntries (
  IN MENU_STATE *M,
  IN OUT APP    *A
)
{
  STATIC ENTRY_VIEW Views[MAX_ENTRIES];
  UINTN             Count = 0;

  VecFree (&mDrives);
  QuirksPrepareStorage ();
  VolumesBoot (&mDrives);
  for (UINTN Index = 0; Index < mDrives.Count && Count < MAX_ENTRIES;
       Index++) {
    EFI_HANDLE Drive = mDrives.Items[Index];

    // A file that isn't a valid Android boot image is skipped.
    if (!BootImageIsOn (Drive)) {
      continue;
    }
    Views[Count] = (ENTRY_VIEW){
      .Kind      = ENTRY_KIND_BOOT_IMAGE,
      .Removable = VolumesMedium (Drive).External,
    };
    AsciiStrCopy (
      Views[Count].Title, sizeof (Views[Count].Title), BootImageTitle (Drive)
    );
    mEntryDrive[Count++] = Drive;
  }
  // Android boots from its own partitions, not a drive the list scanned.
  if (M->Android && Count < MAX_ENTRIES) {
    Views[Count] = (ENTRY_VIEW){.Kind = ENTRY_KIND_ANDROID};
    AsciiStrCopy (
      Views[Count].Title, sizeof (Views[Count].Title), gStrings->Android
    );
    mEntryDrive[Count++] = NULL;
  }
  MenuLog ("boot list: %llu systems", (UINT64)Count);
  AppSetEntries (A, Views, Count);
}

// Stores row Index of the boot list as the next start's pick.
STATIC VOID
PickEntry (IN UINTN Index)
{
  OABL_BUFFER Path = {0};
  EFI_STATUS  Status;

  if (!mEntryDrive[Index] || !PathsHandlePath (mEntryDrive[Index], &Path)) {
    return;
  }
  Status = MenuVarPut (MENU_PICK_VAR, Path.Data, Path.Len);
  MenuFlush ();
  BufferFree (&Path);
  MenuLog ("boot list: row %llu picked (%s)", (UINT64)Index, MENU_STATUS (Status));
}

// The boot list's last pick and whether it was on the SD card, for its
// error screen. Gone after a restart.
STATIC CHAR8   mPicked[sizeof (((ENTRY_VIEW *)0)->Title)];
STATIC BOOLEAN mPickedFromSd;

// Gets what the menu shows about the last start. Returns FALSE if there is
// nothing to show.
STATIC BOOLEAN
CameBackShown (
  IN CONST MENU_STATE *M,
  OUT BOOT_ERROR      *Error,
  OUT CONST CHAR8    **Detail
)
{
  CONST CHAR8 *What;

  *Detail = "";
  switch (M->CameBack) {
  case MENU_START_RESULT_NOTHING:
    What   = "nothing to boot";
    *Error = BOOT_ERROR_NO_IMAGE;
    break;
  case MENU_START_RESULT_FAILED:
    What    = "the boot image failed";
    *Error  = BOOT_ERROR_LOAD_FAILED;
    *Detail = gStrings->ErrBootImageFailed;
    break;
  case MENU_START_RESULT_ANDROID_FAILED:
    What   = "Android didn't start";
    *Error = BOOT_ERROR_ANDROID_FAILED;
    break;
  case MENU_START_RESULT_PICK_GONE:
    What    = "the boot list's pick: its drive is gone";
    *Error  = BOOT_ERROR_PICK_GONE;
    *Detail = mPicked;
    break;
  case MENU_START_RESULT_PICK_FAILED:
    What    = "the boot list's pick didn't start";
    *Error  = BOOT_ERROR_PICK_FAILED;
    *Detail = mPicked;
    break;
  default:
    return FALSE;
  }
  MenuLog ("the start came back: %s", What);
  return TRUE;
}

// Commands

// Keeps PortareOS or Android for the next boot only (MENU_ONCE_VAR), so the
// restart starts it whatever the Boot order says.
STATIC EFI_STATUS
KeepTarget (IN BOOLEAN Linux)
{
  UINT8      Once   = Linux ? MENU_ONCE_LINUX : MENU_ONCE_ANDROID;
  EFI_STATUS Status = MenuVarPut (MENU_ONCE_VAR, &Once, 1);

  MenuFlush ();
  MenuLog (
    "next boot: %s (%s)", Linux ? "PortareOS" : "Android", MENU_STATUS (Status)
  );
  return Status;
}

// Clears what KeepTarget and the boot list kept, after a restart that
// didn't happen, so a later boot doesn't take them.
STATIC VOID
ForgetTarget (VOID)
{
  MenuVarPut (MENU_ONCE_VAR, NULL, 0);
  MenuVarPut (MENU_PICK_VAR, NULL, 0);
  MenuFlush ();
}

// Static, as the app state is large.
STATIC APP mApp;

// What Install PortareOS offers, for the app.
STATIC INSTALL_INFO
InstallInfo (VOID)
{
  INSTALL_SURVEY Survey;
  INSTALL_INFO   Info = {0};
  UINT64         Min;
  UINT64         Max;

  InstallSurvey (&Survey, gMenuEfiLog);
  Info.Card      = Survey.Card;
  Info.Known     = Survey.Internal;
  Info.Android   = Survey.Internal && Survey.Gpt.Android;
  Info.PortareOs = Survey.Internal && Survey.Gpt.PortareOs;
  Info.Update    = InstallKeepsStorage (&Survey);
  Info.Dualboot  = InstallCan (&Survey, INSTALL_DUALBOOT);
  Info.Replace   = InstallCan (&Survey, INSTALL_REPLACE_ANDROID);
  Info.BootBytes = INSTALL_BOOT_BYTES;
  if (InstallAndroidRange (&Survey, &Min, &Max)) {
    Info.AndroidMin = Min;
    Info.AndroidMax = Max;
    Info.Shared     = (Survey.Gpt.UserdataLast - Survey.Gpt.UserdataFirst + 1) *
                  Survey.BlockSize;
  }
  if (Info.Update) {
    Info.UpdateStorage = InstallStorageBytes (&Survey, INSTALL_DUALBOOT, 0);
  }
  Info.ReplaceStorage =
    InstallStorageBytes (&Survey, INSTALL_REPLACE_ANDROID, 0);
  return Info;
}

// Redraws the installing screen when the shown percent changes.
STATIC VOID
OnInstallProgress (
  IN INSTALL_STEP Step,
  IN UINT32       Permille,
  IN VOID        *Context
)
{
  INSTALL_STAGE Stage = Step == INSTALL_STEP_STORAGE ? INSTALL_STAGE_STORAGE
                        : Step == INSTALL_STEP_COPY  ? INSTALL_STAGE_COPY
                        : Step == INSTALL_STEP_CHECK ? INSTALL_STAGE_CHECK
                                                     : INSTALL_STAGE_PREPARE;

  AppOnInstallProgress (&mApp, Stage, Permille);
  if (mApp.Dirty) {
    ViewSync (&mApp);
    ViewRun ();
  }
}

// Runs a command. Returns TRUE to leave the menu, for PortareOS to boot
// without a restart.
STATIC BOOLEAN
RunCommand (
  IN OUT MENU_STATE *M,
  IN COMMAND         Command
)
{
  switch (Command.Kind) {
  case COMMAND_NONE:
    break;
  // Where possible, leave the menu with the start frame up and boot
  // PortareOS without a restart. Otherwise START restarts and boots the
  // system kept for it, and the host only returns if it couldn't. The Boot
  // order stays.
  case COMMAND_BOOT:
  case COMMAND_BOOT_ENTRY: {
    BOOT_TARGET Target    = (BOOT_TARGET)Command.Arg;
    BOOLEAN     ModeLinux = M->BootMode == BOOT_MODE_LINUX;
    // Boot list entries other than Android's are PortareOS's.
    BOOLEAN      Linux = Command.Kind == COMMAND_BOOT_ENTRY ||
                         Target == BOOT_TARGET_PORTAREOS ||
                         (Target == BOOT_TARGET_ORDER && ModeLinux);
    EFI_STATUS   Status;
    CONST CHAR8 *Failed = gStrings->AblRestartFailed;
    CONST CHAR8 *Args[1];
    CHAR8        Why[192];

    if (Command.Kind == COMMAND_BOOT_ENTRY &&
        (UINTN)Command.Arg < mApp.EntryCount) {
      AsciiStrCopy (mPicked, sizeof (mPicked), mApp.Entries[Command.Arg].Title);
      mPickedFromSd = mApp.Entries[Command.Arg].Removable;
      PickEntry ((UINTN)Command.Arg);
    }
    // Only from fastboot's menu; the menu runs nowhere else.
    if (Linux && gFastbootUp) {
      return TRUE;
    }
    Status = KeepTarget (Linux);
    if (EFI_ERROR (Status) && Linux != ModeLinux) {
      // The restart would start the Boot order's system instead.
      Failed = gStrings->TargetNotKept;
    } else {
      Status = Act (M, OPENABL_MENU_ACTION_START);
    }
    ForgetTarget ();
    Args[0] = MENU_STATUS (Status);
    LangFill (Why, sizeof (Why), Failed, Args, 1);
    AppOnFailed (&mApp, Why);
    break;
  }
  case COMMAND_SAVE_BOOT_MODE: {
    CONST CHAR8 *Failure = SaveBootMode (M, mApp.BootMode);

    AppSetBootMode (&mApp, M->BootMode);
    if (Failure) {
      AppOnFailed (&mApp, Failure);
    }
    break;
  }
  case COMMAND_REBOOT:
    if (Act (
          M, (REBOOT_TARGET)Command.Arg == REBOOT_EDL
               ? OPENABL_MENU_ACTION_EMERGENCY
               : OPENABL_MENU_ACTION_START
        ) != EFI_SUCCESS) {
      AppOnFailed (&mApp, gStrings->AblActionFailed);
    }
    break;
  case COMMAND_POWER_OFF:
    if (Act (M, OPENABL_MENU_ACTION_POWER_OFF) != EFI_SUCCESS) {
      AppOnFailed (&mApp, gStrings->PowerOffFailed);
    }
    break;
  case COMMAND_INSTALL: {
    BOOLEAN      Replace = Command.Arg == INSTALL_CHOICE_REPLACE;
    GPT_SURVEY   Internal;
    EFI_STATUS   Status;
    CONST CHAR8 *Args[1];
    CONST CHAR8 *Why;
    CHAR8        Text[192];

    MenuLog (
      "install: %s, Android keeps %llu MiB",
      Replace ? "Replace Android" : "Dualboot", mApp.InstallAndroid >> 20
    );
    Status = InstallRun (
      Replace ? INSTALL_REPLACE_ANDROID : INSTALL_DUALBOOT,
      mApp.InstallAndroid, OnInstallProgress, NULL, gMenuEfiLog
    );
    // What's on internal storage now, for the menu and the boot.
    if (InstallInternal (&Internal)) {
      M->Android = Internal.Android;
    }
    AppSetAndroid (&mApp, M->Android);
    {
      INSTALL_INFO Info = InstallInfo ();
      AppSetInstall (&mApp, &Info);
    }
    if (Status == EFI_SUCCESS) {
      // The new PortareOS starts first, whatever started first before.
      (VOID)SaveBootMode (M, BOOT_MODE_LINUX);
      AppSetBootMode (&mApp, M->BootMode);
      AppOnInstalled (&mApp, TRUE, NULL, TRUE);
      break;
    }
    // InstallRun's status says which step failed, and whether anything
    // was written.
    Why = Status == EFI_NOT_FOUND          ? gStrings->InstallNoCard
          : Status == EFI_VOLUME_CORRUPTED ? gStrings->InstallUntrusted
          : Status == EFI_VOLUME_FULL      ? gStrings->InstallNoRoom
          : Status == EFI_NOT_READY        ? gStrings->InstallLowBattery
          : Status == EFI_CRC_ERROR        ? gStrings->InstallReadBack
          : Status == EFI_OUT_OF_RESOURCES ? gStrings->InstallNoMemory
                                           : gStrings->InstallWriteFailed;
    Args[0] = MENU_STATUS (Status);
    LangFill (Text, sizeof (Text), Why, Args, 1);
    AppOnInstalled (
      &mApp, FALSE, Text,
      Status != EFI_NOT_FOUND && Status != EFI_VOLUME_CORRUPTED &&
        Status != EFI_VOLUME_FULL && Status != EFI_NOT_READY
    );
    break;
  }
  }
  return FALSE;
}

// Menu loop

STATIC DISPLAY mDisplay;

// The charging screen reads the battery every second, turns the device off
// once three reads in a row find no charger, and turns the display off a
// few seconds after it shows or after the last key.
#define BATTERY_EVERY_MS 1000
#define UNPLUGGED_READS  3
#define DARK_AFTER_MS    5000
// Between its checks the charging screen waits this long with the CPU idle.
#define CHARGING_IDLE_MS 50

STATIC UINT64    mBatteryAt;
STATIC UINT64    mAwakeAt;
STATIC UINT32    mUnplugged;
STATIC BOOLEAN   mDark;
STATIC EFI_EVENT mIdleTimer;

// Turns a dark display back on and draws it all again.
STATIC VOID
Wake (IN UINT64 Now)
{
  EFI_STATUS Status = DisplaySetPower (TRUE);

  MenuLog ("charging: display on (%s)", MENU_STATUS (Status));
  mDark      = FALSE;
  mAwakeAt   = Now;
  mApp.Dirty = TRUE;
  ViewSync (&mApp);
  ViewRedraw (TRUE);
}

// The charging screen's checks, between frames.
STATIC VOID
ChargingTick (
  IN MENU_STATE *M,
  IN UINT64      Now
)
{
  BATTERY_STATE Battery;
  BOOLEAN       Known;

  if (Now >= mBatteryAt) {
    mBatteryAt = Now + BATTERY_EVERY_MS;
    Known      = BatteryRead (&Battery);
    AppOnBattery (&mApp, Known, Battery.Charger, Battery.MilliVolts);
    mUnplugged = Known && !Battery.Charger ? mUnplugged + 1 : 0;
    if (mUnplugged >= UNPLUGGED_READS) {
      MenuLog ("charging: the charger is out at %u mV; turning off",
               Battery.MilliVolts);
      MenuFlush ();
      Act (M, OPENABL_MENU_ACTION_POWER_OFF);
      mUnplugged = 0;
    }
  }
  if (!mDark && Now - mAwakeAt >= DARK_AFTER_MS) {
    EFI_STATUS Status = DisplaySetPower (FALSE);
    MenuLog ("charging: display off (%s)", MENU_STATUS (Status));
    // If it didn't go off, it isn't tried again until the next key.
    mDark    = Status == EFI_SUCCESS;
    mAwakeAt = Now;
  }
}

// Waits Ms with the CPU idle. FALSE if it can't, so the caller spins.
STATIC BOOLEAN
IdleWait (IN UINT64 Ms)
{
  UINTN Index;

  if (!mIdleTimer &&
      gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &mIdleTimer) !=
        EFI_SUCCESS) {
    mIdleTimer = NULL;
    return FALSE;
  }
  return gBS->SetTimer (mIdleTimer, TimerRelative, Ms * 10000) ==
           EFI_SUCCESS &&
         gBS->WaitForEvent (1, &mIdleTimer, &Index) == EFI_SUCCESS;
}

STATIC VOID
BlitToPanel (
  IN CONST UINT32 *Px,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  DisplayBlit (&mDisplay, Px, Stride, X, Y, W, H);
}

STATIC UINT32
NowMs (VOID)
{
  return (UINT32)ClockNowMs ();
}

// Commands waiting to run, oldest first.
typedef struct _PENDING_COMMANDS {
  COMMAND Items[8];
  UINTN   Count;
} PENDING_COMMANDS;

// Queues Command, unless it is COMMAND_NONE or the queue is full.
STATIC VOID
Pend (
  IN OUT PENDING_COMMANDS *Pending,
  IN COMMAND               Command
)
{
  if (Command.Kind != COMMAND_NONE &&
      Pending->Count < sizeof (Pending->Items) / sizeof (*Pending->Items)) {
    Pending->Items[Pending->Count++] = Command;
  }
}

// Takes the oldest command off a non-empty queue.
STATIC COMMAND
NextPending (IN OUT PENDING_COMMANDS *Pending)
{
  COMMAND Command = Pending->Items[0];

  for (UINTN Index = 1; Index < Pending->Count; Index++) {
    Pending->Items[Index - 1] = Pending->Items[Index];
  }
  Pending->Count--;
  return Command;
}

MENU_END
MenuRun (
  IN CONST MENU_HOST *Host,
  IN UINT8            CameBack
)
{
  STATIC MENU_STATE M;
  STATIC CHAR8      Version[64];
  INPUT_KEYS        Keys    = InputKeysNew ();
  PENDING_COMMANDS  Pending = {0};
  PANEL             Panel;
  EFI_STATUS        Status;
  UINTN             PanelW;
  UINTN             PanelH;

  Status = MenuNew (&M, Host, CameBack);
  if (EFI_ERROR (Status)) {
    MenuLog ("reading the ABL's state failed: %s", MENU_STATUS (Status));
    return (MENU_END){MENU_END_FAILED, Status};
  }
  Status = DisplayOpen (&mDisplay);
  if (EFI_ERROR (Status)) {
    MenuHostLog (&M.Host, "openabl-menu: no graphics output");
    return (MENU_END){MENU_END_FAILED, Status};
  }
  DisplaySize (&mDisplay, &PanelW, &PanelH);
  Panel = (PANEL){.W       = (UINT32)PanelW,
                  .H       = (UINT32)PanelH,
                  .Present = BlitToPanel,
                  .Now     = NowMs};
  if (!ViewInit (&Panel)) {
    MenuHostLog (&M.Host, "openabl-menu: no memory for the view");
    ViewDone ();
    return (MENU_END){MENU_END_FAILED, EFI_LOAD_ERROR};
  }
  // The header shows the ABL's version.
  AsciiStrCopy (
    Version, sizeof (Version), M.AblVersion[0] ? M.AblVersion : MENU_VERSION
  );
  AppInit (&mApp, M.BootMode, M.Android, Version);
  {
    INSTALL_INFO Install = InstallInfo ();
    AppSetInstall (&mApp, &Install);
  }
  FillEntries (&M, &mApp);
  AppTick (&mApp, ClockNowMs ());
  if (M.CameBack == MENU_START_RESULT_CHARGING) {
    MenuLog ("charging: powered on by the charger");
    AppStartCharging (&mApp);
    mBatteryAt = 0;
    mAwakeAt   = ClockNowMs ();
    mUnplugged = 0;
  }
  {
    BOOT_ERROR   Error;
    CONST CHAR8 *Detail;
    if (CameBackShown (&M, &Error, &Detail)) {
      if (Error == BOOT_ERROR_PICK_GONE || Error == BOOT_ERROR_PICK_FAILED) {
        AppOnPickFailed (
          &mApp, Error == BOOT_ERROR_PICK_GONE, Detail, mPickedFromSd
        );
      } else {
        AppOnBootResult (&mApp, Error, Detail);
      }
    }
  }
  MenuLog ("display %llux%llu", (UINT64)PanelW, (UINT64)PanelH);

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  // Ignore a key still held from opening the menu.
  InputKeysHeldAtStart (&Keys);
  ViewRotate (NOVA_ROTATION);
  mApp.Dirty = TRUE;

  for (;;) {
    UINT64       Now = ClockNowMs ();
    UINT64       Until;
    INPUT_EVENTS Events = {0};

    InputKeysPoll (&Keys, Now, &Events);
    // A key wakes a dark charging screen and does nothing else.
    if (Events.Count) {
      if (mDark) {
        Wake (Now);
        Events.Count = 0;
      }
      mAwakeAt = Now;
    }
    for (UINTN Index = 0; Index < Events.Count; Index++) {
      Pend (&Pending, AppInput (&mApp, Events.Items[Index]));
    }
    VecFree (&Events);
    Pend (&Pending, AppTick (&mApp, Now));
    if (mApp.Screen == SCREEN_CHARGING) {
      ChargingTick (&M, Now);
    } else if (mDark) {
      Wake (Now);
    }
    if (mApp.Dirty) {
      ViewSync (&mApp);
    }
    ViewRun ();
    // Commands run after the frame is on screen.
    if (Pending.Count) {
      ViewSettle ();
      if (RunCommand (&M, NextPending (&Pending))) {
        ViewDone ();
        return (MENU_END){MENU_END_START_HERE, EFI_ABORTED};
      }
      continue;
    }
    // While charging, a poll, then the CPU idles: the device may sit on this
    // screen for hours.
    if (mApp.Screen == SCREEN_CHARGING) {
      Status = MenuHostPoll (&M.Host);
      if (Status != EFI_SUCCESS) {
        MenuLog ("the ABL asked to leave (%s)", MENU_STATUS (Status));
        ViewDone ();
        return (MENU_END){MENU_END_LEFT, Status};
      }
      if (IdleWait (CHARGING_IDLE_MS)) {
        continue;
      }
    }
    // Give idle time to fastboot. The ABL serves one USB event per poll,
    // and a download needs polls back to back.
    Until = ClockNowMs () + 8;
    for (;;) {
      Status = MenuHostPoll (&M.Host);
      if (Status != EFI_SUCCESS) {
        MenuLog ("the ABL asked to leave (%s)", MENU_STATUS (Status));
        ViewDone ();
        return (MENU_END){MENU_END_LEFT, Status};
      }
      if (ClockNowMs () >= Until) {
        break;
      }
    }
  }
}
