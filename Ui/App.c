/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  App - the menu's state machine (Ui/Ui.h).
*/

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"

// How long a toast shows, in milliseconds.
#define TOAST_MS 2500

STATIC COMMAND
None (VOID)
{
  return (COMMAND){.Kind = COMMAND_NONE};
}

STATIC COMMAND
Command (
  IN COMMAND_KIND Kind,
  IN INT32        Arg
)
{
  return (COMMAND){.Kind = Kind, .Arg = Arg};
}

VOID
AppInit (
  OUT APP        *A,
  IN BOOT_MODE    BootMode,
  IN BOOLEAN      Android,
  IN CONST CHAR8 *Version
)
{
  ZeroMem (A, sizeof (*A));
  A->BootMode  = BootMode;
  A->Android   = Android;
  A->Version   = Version;
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_BOOT;
  A->Booting   = -1;
  A->Dirty     = TRUE;
}

VOID
AppSetBootMode (
  IN OUT APP  *A,
  IN BOOT_MODE BootMode
)
{
  A->BootMode = BootMode;
  A->Dirty    = TRUE;
}

VOID
AppSetAndroid (
  IN OUT APP *A,
  IN BOOLEAN  Android
)
{
  A->Android = Android;
  A->Dirty   = TRUE;
}

VOID
AppSetEntries (
  IN OUT APP          *A,
  IN CONST ENTRY_VIEW *Entries,
  IN UINTN             Count
)
{
  if (Count > MAX_ENTRIES) {
    Count = MAX_ENTRIES;
  }
  for (UINTN Index = 0; Index < Count; Index++) {
    A->Entries[Index] = Entries[Index];
  }
  A->EntryCount = Count;
  A->EntryList  = (LIST_STATE){0};
  A->Dirty      = TRUE;
}

STATIC VOID
ShowToast (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
)
{
  AsciiStrCopy (A->Toast, sizeof (A->Toast), Message);
  A->ToastUntil = A->Now + TOAST_MS;
  A->Dirty      = TRUE;
}

// Starts Target: PortareOS, Android, or the first of the boot order. Shows
// the error screen instead when Android can't be booted here.
STATIC COMMAND
BeginBoot (
  IN OUT APP *A,
  IN BOOT_TARGET Target
)
{
  if (Target == BOOT_TARGET_ORDER) {
    Target = A->BootMode == BOOT_MODE_ANDROID ? BOOT_TARGET_ANDROID
                                              : BOOT_TARGET_PORTAREOS;
  }
  A->Starting = Target;
  A->Dirty    = TRUE;
  if (Target == BOOT_TARGET_ANDROID && !A->Android) {
    A->Screen = SCREEN_ERROR;
    A->Error  = BOOT_ERROR_ANDROID;
    return None ();
  }
  A->Screen  = SCREEN_BOOTING;
  A->Booting = -1;
  return Command (COMMAND_BOOT, Target);
}

COMMAND
AppTick (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  A->Now = Now;
  if (A->ToastUntil && Now >= A->ToastUntil) {
    A->ToastUntil = 0;
    A->Dirty      = TRUE;
  }
  return None ();
}

// Results from the platform

VOID
AppOnBootResult (
  IN OUT APP     *A,
  IN BOOT_ERROR   Error,
  IN CONST CHAR8 *Detail OPTIONAL
)
{
  A->Dirty  = TRUE;
  A->Screen = SCREEN_ERROR;
  A->Error  = Error;
  AsciiStrCopy (A->ErrorDetail, sizeof (A->ErrorDetail), Detail ? Detail : "");
}

VOID
AppOnPickFailed (
  IN OUT APP     *A,
  IN BOOLEAN      Gone,
  IN CONST CHAR8 *Name,
  IN BOOLEAN      FromSd
)
{
  A->Dirty      = TRUE;
  A->Screen     = SCREEN_ERROR;
  A->Error      = Gone ? BOOT_ERROR_PICK_GONE : BOOT_ERROR_PICK_FAILED;
  A->PickFromSd = FromSd;
  AsciiStrCopy (A->ErrorDetail, sizeof (A->ErrorDetail), Name);
}

VOID
AppSetInstall (
  IN OUT APP            *A,
  IN CONST INSTALL_INFO *Info
)
{
  A->Install = *Info;
  A->Dirty   = TRUE;
}

// A first Dualboot's choices for Android, in GB as storage is sold.
STATIC CONST UINT64 mAndroidGb[] = {8, 16, 32, 64, 128};
#define DEFAULT_ANDROID_GB 16
#define GB                 1000000000ull
// The installer starts PortareOS on a 4 MiB boundary after Android.
#define ANDROID_ALIGN (4ull << 20)

UINTN
AppAndroidSizes (
  IN CONST APP *A,
  OUT UINT64   *Sizes,
  OUT UINTN    *Default
)
{
  CONST INSTALL_INFO *Info  = &A->Install;
  UINTN               Count = 0;

  *Default = 0;
  for (UINTN Index = 0; Index < sizeof (mAndroidGb) / sizeof (*mAndroidGb) &&
                        Count < MAX_ANDROID_SIZES;
       Index++) {
    UINT64 Bytes = mAndroidGb[Index] * GB;
    if (Bytes < Info->AndroidMin || Bytes > Info->AndroidMax) {
      continue;
    }
    if (mAndroidGb[Index] <= DEFAULT_ANDROID_GB) {
      *Default = Count;
    }
    Sizes[Count++] = Bytes;
  }
  // Too little room for any of them: the least Android can keep.
  if (Count == 0 && Info->AndroidMin && Info->AndroidMin <= Info->AndroidMax) {
    Sizes[Count++] = Info->AndroidMin;
  }
  return Count;
}

UINT64
AppDualbootStorage (
  IN CONST APP *A,
  IN UINT64     AndroidBytes
)
{
  CONST INSTALL_INFO *Info = &A->Install;
  UINT64              Taken =
    (AndroidBytes + ANDROID_ALIGN - 1) / ANDROID_ALIGN * ANDROID_ALIGN +
    Info->BootBytes;

  return Info->Shared > Taken ? Info->Shared - Taken : 0;
}

VOID
AppOnInstallProgress (
  IN OUT APP       *A,
  IN INSTALL_STAGE  Stage,
  IN UINT32         Permille
)
{
  if (Stage != A->InstallStage || Permille / 10 != A->InstallPermille / 10) {
    A->Dirty = TRUE;
  }
  A->InstallStage    = Stage;
  A->InstallPermille = Permille;
}

VOID
AppOnInstalled (
  IN OUT APP     *A,
  IN BOOLEAN      Ok,
  IN CONST CHAR8 *Why OPTIONAL,
  IN BOOLEAN      Changed
)
{
  A->Dirty          = TRUE;
  A->ToastUntil     = 0;
  A->InstallChanged = Changed;
  if (Ok) {
    A->Screen = SCREEN_INSTALLED;
    return;
  }
  A->Screen = SCREEN_ERROR;
  A->Error  = BOOT_ERROR_INSTALL;
  AsciiStrCopy (A->ErrorDetail, sizeof (A->ErrorDetail), Why ? Why : "");
}

VOID
AppStartCharging (IN OUT APP *A)
{
  A->Screen     = SCREEN_CHARGING;
  A->ToastUntil = 0;
  A->Dirty      = TRUE;
}

// Open-circuit voltage to charge for a 4.5 V lithium-ion cell, in mV and
// percent: at rest the Nova read 4497 mV at 99%.
STATIC CONST UINT32 mCharge[][2] = {
  {3400, 0},  {3600, 3},  {3700, 8},  {3750, 15}, {3800, 25},
  {3850, 35}, {3900, 45}, {3950, 52}, {4000, 58}, {4100, 68},
  {4200, 78}, {4300, 87}, {4400, 95}, {4480, 100},
};
#define CHARGE_POINTS (sizeof (mCharge) / sizeof (mCharge[0]))
// What charging adds to the voltage, roughly.
#define CHARGING_MV 60

UINT32
AppBatteryLevel (IN CONST APP *A)
{
  UINT32 Mv = A->BatteryMv;

  if (!A->BatteryKnown || Mv == 0) {
    return 0;
  }
  if (A->Charger) {
    Mv = Mv > CHARGING_MV ? Mv - CHARGING_MV : 0;
  }
  if (Mv <= mCharge[0][0]) {
    return 0;
  }
  for (UINTN Index = 1; Index < CHARGE_POINTS; Index++) {
    if (Mv < mCharge[Index][0]) {
      UINT32 Low  = mCharge[Index - 1][0];
      UINT32 High = mCharge[Index][0];
      return mCharge[Index - 1][1] + (mCharge[Index][1] - mCharge[Index - 1][1]) *
                                       (Mv - Low) / (High - Low);
    }
  }
  return 100;
}

VOID
AppOnBattery (
  IN OUT APP *A,
  IN BOOLEAN  Known,
  IN BOOLEAN  Charger,
  IN UINT32   MilliVolts
)
{
  UINT32 Before = AppBatteryLevel (A) / 10;

  if (Known != A->BatteryKnown || Charger != A->Charger) {
    A->Dirty = TRUE;
  }
  A->BatteryKnown = Known;
  A->Charger      = Charger;
  A->BatteryMv    = MilliVolts;
  if (AppBatteryLevel (A) / 10 != Before) {
    A->Dirty = TRUE;
  }
}

VOID
AppOnFailed (
  IN OUT APP     *A,
  IN CONST CHAR8 *Message
)
{
  if (A->Screen == SCREEN_BOOTING || A->Screen == SCREEN_CONFIRM ||
      A->Screen == SCREEN_INSTALLING) {
    A->Screen = SCREEN_MENU;
  }
  ShowToast (A, Message);
}

// What the view shows

CONST CHAR8 *
AppBootTitle (IN CONST APP *A)
{
  return A->Booting >= 0 && (UINTN)A->Booting < A->EntryCount
           ? A->Entries[A->Booting].Title
           : NULL;
}

UINTN
AppRows (
  IN CONST APP *A,
  OUT ROW      *Rows
)
{
  UINTN Count = 0;

#define ADD_HEADER(Title) (Rows[Count++] = (ROW){.Header = (Title)})
#define ADD_ITEM(Id)      (Rows[Count++] = (ROW){.Item = (Id)})
  ADD_HEADER (gStrings->SectionStart);
  ADD_ITEM (ITEM_BOOT);
  if (A->Android) {
    ADD_ITEM (ITEM_BOOT_ORDER);
  }
  ADD_HEADER (gStrings->SectionStorage);
  ADD_ITEM (ITEM_INSTALL);
  ADD_HEADER (gStrings->SectionSystem);
  ADD_ITEM (ITEM_EDL);
  ADD_ITEM (ITEM_RESTART);
  ADD_ITEM (ITEM_POWER_OFF);
#undef ADD_HEADER
#undef ADD_ITEM
  return Count;
}

// Fills Items (MAX_ROWS long) with the menu items, without headers.
STATIC UINTN
MenuItems (
  IN CONST APP *A,
  OUT ITEM     *Items
)
{
  ROW   Rows[MAX_ROWS];
  UINTN RowCount = AppRows (A, Rows);
  UINTN Count    = 0;

  for (UINTN Index = 0; Index < RowCount; Index++) {
    if (!Rows[Index].Header) {
      Items[Count++] = Rows[Index].Item;
    }
  }
  return Count;
}

UINTN
AppFocusedRow (
  IN CONST APP *A,
  IN CONST ROW *Rows,
  IN UINTN      Count
)
{
  for (UINTN Index = 0; Index < Count; Index++) {
    if (!Rows[Index].Header && Rows[Index].Item == A->FocusItem) {
      return Index;
    }
  }
  return 1;
}

VOID
AppItemView (
  IN CONST APP  *A,
  IN ITEM        Item,
  OUT ITEM_VIEW *View
)
{
  CHAR8 *Value = View->Value;
  UINTN  Cap   = sizeof (View->Value);

  *View = (ITEM_VIEW){.Verb = gStrings->VerbSelect};
  switch (Item) {
  case ITEM_BOOT:
    View->Label = gStrings->Boot;
    View->Help  = gStrings->HelpBoot;
    break;
  case ITEM_BOOT_ORDER:
    View->Label = gStrings->BootOrder;
    View->Verb  = gStrings->VerbChange;
    View->Help  = gStrings->HelpBootOrder;
    AsciiStrCopy (
      Value, Cap,
      A->BootMode == BOOT_MODE_ANDROID ? gStrings->OrderAndroidFirst
                                       : gStrings->OrderPortareOsFirst
    );
    break;
  case ITEM_INSTALL:
    View->Label = gStrings->InstallPortareOs;
    View->Help  = gStrings->HelpInstall;
    break;
  case ITEM_EDL:
    View->Label = gStrings->Edl;
    View->Help  = gStrings->HelpEdl;
    break;
  case ITEM_RESTART:
    View->Label = gStrings->Restart;
    break;
  case ITEM_POWER_OFF:
    View->Label = gStrings->ShutDown;
    break;
  }
}

// Input

// What Select does on a menu item.
STATIC COMMAND
Activate (
  IN OUT APP *A,
  IN ITEM     Item
)
{
  switch (Item) {
  case ITEM_BOOT:
    A->Screen    = SCREEN_ENTRIES;
    A->EntryList = (LIST_STATE){0};
    return None ();
  case ITEM_BOOT_ORDER:
    A->BootMode = A->BootMode == BOOT_MODE_ANDROID ? BOOT_MODE_LINUX
                                                   : BOOT_MODE_ANDROID;
    return Command (COMMAND_SAVE_BOOT_MODE, 0);
  case ITEM_INSTALL:
    if (!A->Install.Card) {
      A->Screen         = SCREEN_ERROR;
      A->Error          = BOOT_ERROR_NO_CARD;
      A->ErrorDetail[0] = 0;
      return None ();
    }
    A->Screen      = SCREEN_INSTALL;
    A->InstallList = (LIST_STATE){
      .Focus = !A->Install.Dualboot && A->Install.Replace ? 1 : 0};
    return None ();
  case ITEM_EDL:
    A->Screen     = SCREEN_CONFIRM;
    A->Confirm    = CONFIRM_EDL;
    A->ConfirmYes = FALSE;
    return None ();
  case ITEM_RESTART:
    return Command (COMMAND_REBOOT, REBOOT_NORMAL);
  case ITEM_POWER_OFF:
    return Command (COMMAND_POWER_OFF, 0);
  }
  return None ();
}

// Up and down move the focus through the items, wrapping around.
STATIC COMMAND
MenuInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  ITEM  Items[MAX_ROWS];
  UINTN Count   = MenuItems (A, Items);
  UINTN Focused = 0;

  for (UINTN Index = 0; Index < Count; Index++) {
    if (Items[Index] == A->FocusItem) {
      Focused = Index;
      break;
    }
  }
  switch (Input) {
  case INPUT_UP:
    A->FocusItem = Items[(Focused + Count - 1) % Count];
    return None ();
  case INPUT_DOWN:
    A->FocusItem = Items[(Focused + 1) % Count];
    return None ();
  case INPUT_BACK:
    return None ();
  case INPUT_SELECT:
    return Activate (A, Items[Focused]);
  }
  return None ();
}

STATIC VOID
LeaveEntries (IN OUT APP *A)
{
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_BOOT;
}

STATIC COMMAND
EntriesInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  // Rows: each entry, then "Back".
  UINTN Count = A->EntryCount + 1;
  UINTN Focus = A->EntryList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->EntryList.Focus = (Focus + Count - 1) % Count;
    break;
  case INPUT_DOWN:
    A->EntryList.Focus = (Focus + 1) % Count;
    break;
  case INPUT_BACK:
    LeaveEntries (A);
    break;
  case INPUT_SELECT:
    if (Focus < A->EntryCount && A->Entries[Focus].Kind == ENTRY_KIND_ANDROID) {
      return BeginBoot (A, BOOT_TARGET_ANDROID);
    }
    if (Focus < A->EntryCount) {
      A->Screen  = SCREEN_BOOTING;
      A->Booting = (INT32)Focus;
      return Command (COMMAND_BOOT_ENTRY, (INT32)Focus);
    }
    LeaveEntries (A);
    break;
  }
  return None ();
}

STATIC VOID
LeaveInstall (IN OUT APP *A)
{
  A->Screen    = SCREEN_MENU;
  A->FocusItem = ITEM_INSTALL;
}

STATIC VOID
ConfirmInstall (
  IN OUT APP     *A,
  IN INSTALL_CHOICE Choice
)
{
  A->InstallChoice = Choice;
  A->Screen        = SCREEN_CONFIRM;
  A->Confirm       = CONFIRM_INSTALL;
  A->ConfirmYes    = FALSE;
}

// Rows: Dualboot, Replace Android, Back. A choice that can't run does
// nothing; its note says why.
STATIC COMMAND
InstallInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  UINTN Focus = A->InstallList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->InstallList.Focus = (Focus + 2) % 3;
    break;
  case INPUT_DOWN:
    A->InstallList.Focus = (Focus + 1) % 3;
    break;
  case INPUT_BACK:
    LeaveInstall (A);
    break;
  case INPUT_SELECT:
    if (Focus == 0 && A->Install.Dualboot) {
      UINT64 Sizes[MAX_ANDROID_SIZES];
      UINTN  Default;

      if (A->Install.Update) {
        ConfirmInstall (A, INSTALL_CHOICE_DUALBOOT);
      } else if (AppAndroidSizes (A, Sizes, &Default)) {
        A->Screen   = SCREEN_INSTALL_SIZE;
        A->SizeList = (LIST_STATE){.Focus = Default};
      }
    } else if (Focus == 1 && A->Install.Replace) {
      ConfirmInstall (A, INSTALL_CHOICE_REPLACE);
    } else if (Focus == 2) {
      LeaveInstall (A);
    }
    break;
  }
  return None ();
}

// Rows: each size Android can keep, then Back.
STATIC COMMAND
SizeInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  UINT64 Sizes[MAX_ANDROID_SIZES];
  UINTN  Default;
  UINTN  Count = AppAndroidSizes (A, Sizes, &Default) + 1;
  UINTN  Focus = A->SizeList.Focus;

  switch (Input) {
  case INPUT_UP:
    A->SizeList.Focus = (Focus + Count - 1) % Count;
    break;
  case INPUT_DOWN:
    A->SizeList.Focus = (Focus + 1) % Count;
    break;
  case INPUT_BACK:
    A->Screen = SCREEN_INSTALL;
    break;
  case INPUT_SELECT:
    if (Focus + 1 < Count) {
      A->InstallAndroid = Sizes[Focus];
      ConfirmInstall (A, INSTALL_CHOICE_DUALBOOT);
    } else {
      A->Screen = SCREEN_INSTALL;
    }
    break;
  }
  return None ();
}

// Up and down switch between Cancel and the action; Back cancels.
STATIC COMMAND
ConfirmInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  switch (Input) {
  case INPUT_UP:
  case INPUT_DOWN:
    A->ConfirmYes = !A->ConfirmYes;
    return None ();
  case INPUT_BACK:
    A->Screen = A->Confirm == CONFIRM_INSTALL ? SCREEN_INSTALL : SCREEN_MENU;
    return None ();
  case INPUT_SELECT:
    A->Screen = SCREEN_MENU;
    if (A->Confirm == CONFIRM_INSTALL) {
      if (!A->ConfirmYes) {
        A->Screen = SCREEN_INSTALL;
        return None ();
      }
      A->Screen          = SCREEN_INSTALLING;
      A->InstallStage    = INSTALL_STAGE_PREPARE;
      A->InstallPermille = 0;
      return Command (COMMAND_INSTALL, (INT32)A->InstallChoice);
    }
    if (!A->ConfirmYes) {
      return None ();
    }
    return Command (COMMAND_REBOOT, REBOOT_EDL);
  }
  return None ();
}

COMMAND
AppInput (
  IN OUT APP *A,
  IN INPUT    Input
)
{
  A->Dirty = TRUE;
  switch (A->Screen) {
  case SCREEN_MENU:
    return MenuInput (A, Input);
  case SCREEN_ENTRIES:
    return EntriesInput (A, Input);
  case SCREEN_CONFIRM:
    return ConfirmInput (A, Input);
  case SCREEN_ERROR:
    A->Screen = SCREEN_MENU;
    return None ();
  case SCREEN_INSTALL:
    return InstallInput (A, Input);
  case SCREEN_INSTALL_SIZE:
    return SizeInput (A, Input);
  case SCREEN_INSTALLED:
    // The menu's view of storage is out of date; only a restart is offered.
    return Input == INPUT_SELECT ? Command (COMMAND_REBOOT, REBOOT_NORMAL)
                                 : None ();
  case SCREEN_CHARGING:
    if (Input == INPUT_SELECT) {
      return BeginBoot (A, BOOT_TARGET_ORDER);
    }
    A->Screen    = SCREEN_MENU;
    A->FocusItem = ITEM_BOOT;
    return None ();
  case SCREEN_BOOTING:
  case SCREEN_INSTALLING:
    return None ();
  }
  return None ();
}

// Keys

BOOLEAN
KeysPress (
  IN OUT KEYS *Keys,
  IN INPUT     Key,
  IN UINT64    Now,
  OUT INPUT   *Out
)
{
  if (Keys->Held && Keys->Key == Key && Now - Keys->Last < KEY_GAP_MS) {
    // A repeat. Only Up and Down act on it, to scroll. Repeating Select or
    // Back would trigger the action again.
    Keys->Last = Now;
    if ((Key == INPUT_UP || Key == INPUT_DOWN) &&
        Now - Keys->Start >= KEY_REPEAT_DELAY_MS &&
        Now - Keys->LastEmit >= KEY_REPEAT_EVERY_MS) {
      Keys->LastEmit = Now;
      *Out           = Key;
      return TRUE;
    }
    return FALSE;
  }
  Keys->Held  = TRUE;
  Keys->Key   = Key;
  Keys->Start = Keys->Last = Keys->LastEmit = Now;
  *Out                                      = Key;
  return TRUE;
}

VOID
KeysIdle (
  IN OUT KEYS *Keys,
  IN UINT64    Now
)
{
  if (Keys->Held && Now - Keys->Last >= KEY_GAP_MS) {
    Keys->Held = FALSE;
  }
}
