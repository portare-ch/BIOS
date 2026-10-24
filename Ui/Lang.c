/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - the menu's strings.
*/

#include "Ui/Lang.h"
#include "Base/Text.h"

STATIC CONST LANG_STRINGS mEn = {
  .SectionSystem     = "System",
  .Boot              = "Boot",
  .Edl               = "Emergency download mode",
  .Internal = "Internal storage",
  .SdCard            = "SD card",
  .SrcInternal       = "internal storage",
  .SrcSd             = "the SD card",
  .Back              = "Back",
  .Starting        = "Starting...",
  .ErrNoImageTitle = "Couldn't start PortareOS",
  .ErrNoImageMsg = "No usable PortareOS installation was found.",
  .ErrNoImageHint = "Insert a PortareOS card, or reinstall PortareOS.",
  .ErrLoadTitle    = "Couldn't start PortareOS",
  .ErrLoadHint = "Its boot files may be damaged. Reinstall or update PortareOS.",
  .ErrBootImageFailed = "PortareOS failed verification or couldn't start.",
  .ErrAndroidTitle       = "Android can't start here",
  .ErrAndroidMsg = "This firmware can't start Android on this device.",
  .ErrAndroidHint = "Choose another system under Boot.",
  .ErrAndroidFailedTitle = "Couldn't start Android",
  .ErrAndroidFailedMsg = "Android didn't start from internal storage.",
  .ErrAndroidFailedHint = "Its partitions may be damaged. Choose another system under Boot.",
  .ConfirmEdlTitle       = "Enter emergency download mode?",
  .ConfirmEdlMsg = "The screen stays off in this mode. It's for Qualcomm flashing tools on a computer.",
  .ConfirmEdl    = "Enter EDL",
  .Cancel        = "Cancel",
  .AblSettingFailed = "Couldn't save the setting.",
  .AblRestartFailed = "Couldn't restart ({}).",
  .TargetNotKept = "Couldn't save the boot choice ({}).",
  .AblActionFailed = "Couldn't complete the request.",
  .PowerOffFailed = "Couldn't shut down.",
  .DontTurnOff         = "Don't turn off the Nova.",
  .InstallNoMemory = "Not enough memory to finish ({}).",
  // BIOS
  .SectionStart = "Start",
  .SectionStorage = "Storage",
  .PortareOs = "PortareOS",
  .Android = "Android",
  .BootOrder = "Boot order",
  .Restart = "Restart",
  .ShutDown = "Shut down",
  .OrderPortareOsFirst = "PortareOS first",
  .OrderAndroidFirst = "Android first",
  .HelpBoot = "Choose a system for this boot only.",
  .FromInternal = "From internal storage",
  .FromSdCard = "From the SD card",
  .ErrPickTitle = "Couldn't start {}",
  .ErrPickTitleAny = "Couldn't start the chosen system",
  .ErrPickGoneSd = "The SD card is no longer available.",
  .ErrPickGoneSdHint = "Reinsert the SD card, then restart into BIOS. Hold Volume Down while powering on.",
  .ErrPickGone = "Internal storage is no longer available.",
  .ErrPickFailed = "{} didn't start from {}.",
  .ErrPickFailedAny = "It didn't start.",
  .ErrPickHint = "Try again or choose another system.",
  .HelpBootOrder = "Sets the default system at power-on.",
  .HelpEdl = "For Qualcomm flashing tools on a computer.",
  .InstallPortareOs = "Install PortareOS",
  .HelpInstall = "Copies PortareOS from the SD card to internal storage. The SD card isn't changed.",
  .Dualboot = "Dualboot",
  .ReplaceAndroid = "Replace Android",
  .ValueKeepAndroid = "Resets Android",
  .ValueUpdate = "Updates PortareOS",
  .ValueAllSpace = "Uses all storage",
  .Unavailable = "Unavailable",
  .NoteDualboot = "Keeps Android but erases its apps and data. Choose how much space Android keeps. PortareOS gets the rest.",
  .NoteUpdate = "Replaces the PortareOS system with the copy on the SD card. Games, saves and settings stay. Android is unchanged.",
  .NoteReplace = "Erases Android and its data. PortareOS gets all of internal storage.",
  .NoteReplaceAll = "Erases Android and PortareOS with all their data, games and saves. PortareOS gets all of internal storage.",
  .NoteNoAndroid = "Android isn't installed.",
  .NoteUnknownLayout = "BIOS doesn't recognize the internal storage layout, so it won't change it.",
  .NoteNoRoom = "Internal storage doesn't have room for both systems.",
  .SizeTitle = "Space for Android",
  .SizeAndroid = "Android {}",
  .SizePortareOs = "PortareOS {}",
  .ConfirmInstallTitle = "Install PortareOS?",
  .ConfirmDualbootMsg = "Resets Android, erasing its apps and data. Each system gets the space shown below.",
  .ConfirmUpdateMsg = "Replaces the PortareOS system with the copy on the SD card. Games, saves and settings stay. Android is unchanged.",
  .ConfirmReplaceMsg = "Erases Android and its data. Restoring Android needs a computer and its original firmware.",
  .ConfirmReplaceAllMsg = "Erases Android and PortareOS with all their data, games and saves. Restoring Android needs a computer and its original firmware.",
  .ConfirmInstall = "Install",
  .Installing = "Installing PortareOS",
  .StagePrepare = "Preparing internal storage... {}%",
  .StageStorage = "Setting up PortareOS storage... {}%",
  .StageCopy = "Copying PortareOS... {}%",
  .StageCheck = "Checking the copy... {}%",
  .Installed = "PortareOS is installed",
  .InstalledMsg = "After a restart, PortareOS starts from internal storage. You can leave the SD card in or take it out.",
  .VerbRestart = "RESTART",
  .NoCardTitle = "No PortareOS SD card",
  .NoCardMsg = "BIOS didn't find a PortareOS SD card at startup.",
  .NoCardHint = "Insert one, then restart into BIOS. Hold Volume Down while powering on.",
  .InstallFailed = "Couldn't install PortareOS",
  .InstallNoCard = "The SD card is no longer available. Reinsert it, then restart into BIOS.",
  .InstallUntrusted = "The storage layout couldn't be verified, so nothing was changed.",
  .InstallNoRoom = "PortareOS doesn't fit in the available space, so nothing was changed.",
  .InstallLowBattery = "The battery is too low to install. Nothing was changed. Charge the Nova, then try again.",
  .InstallReadBack = "The data on internal storage didn't match what was written.",
  .InstallWriteFailed = "Couldn't write to internal storage ({}).",
  .InstallChangedHint = "PortareOS isn't installed. Try again.",
  .KeyPower = "POWER",
  .KeyVolume = "VOL+/-",
  .VerbLaunch = "START",
  .VerbChange = "CHANGE",
  .VerbSelect = "SELECT",
  .VerbMenu = "MENU",
  .VerbMove = "MOVE",
};

CONST LANG_STRINGS *gStrings = &mEn;

VOID
LangFill (
  OUT CHAR8             *Out,
  IN UINTN               Cap,
  IN CONST CHAR8        *Template,
  IN CONST CHAR8 *CONST *Args,
  IN UINTN               ArgCount
)
{
  UINTN Length = 0;
  UINTN Next   = 0;

  if (!Cap) {
    return;
  }
  while (*Template && Length + 1 < Cap) {
    CONST CHAR8 *Arg = NULL;

    if (Template[0] == '{' && Template[1] == '}') {
      Arg = Next < ArgCount ? Args[Next] : "";
      Next++;
      Template += 2;
    } else if (Template[0] == '{' && Template[1] >= '0' && Template[1] <= '9' &&
               Template[2] == '}') {
      UINTN Which = (UINTN)(Template[1] - '0');
      Arg         = Which < ArgCount ? Args[Which] : "";
      Template += 3;
    }
    if (Arg) {
      while (*Arg && Length + 1 < Cap) {
        Out[Length++] = *Arg++;
      }
    } else {
      Out[Length++] = *Template++;
    }
  }
  Out[Length] = 0;
}
