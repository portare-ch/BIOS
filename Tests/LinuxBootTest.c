/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

#include "Core/LinuxBoot.h"
#include "Base/Text.h"
#include "Tests/Test.h"
#include "Ui/Ui.h"
#include "Ui/Lang.h"

STATIC CONST CHAR8 mPartUuid[] = "01234567-89ab-cdef-0123-456789abcdef";

STATIC VOID
CommandLines (VOID)
{
  STATIC CONST CHAR8 *CONST Lines[] = {
    ("ostree=/ostree/boot.1/default/deployment/0 root=UUID=armada-root "
     "rw rootflags=subvol=root quiet"),
    "boot=LABEL=ROCKNIX disk=LABEL=STORAGE quiet",
    "",
  };
  OABL_BUFFER Out = {0};
  CHAR8       Long[1537];

  for (UINTN Distro = LINUX_DISTRO_UNKNOWN; Distro <= LINUX_DISTRO_ROCKNIX;
       Distro++) {
    if (Distro == LINUX_DISTRO_PORTAREOS) {
      continue;
    }
    for (UINTN Index = 0; Index < sizeof (Lines) / sizeof (*Lines); Index++) {
      CHECK (LinuxBootCmdline (Distro, Lines[Index], mPartUuid, &Out));
      CHECK (Out.Data && AsciiStrEq ((CHAR8 *)Out.Data, Lines[Index]));
      CHECK (Out.Len == AsciiStrLen (Lines[Index]) + 1);
      BufferFree (&Out);
    }
  }

  // Keep arguments beyond the first 512-byte header field. The parsed
  // command line can use all 1536 bytes of cmdline and extra_cmdline.
  for (UINTN Index = 0; Index < sizeof (Long) - 1; Index++) {
    Long[Index] = 'a';
  }
  Long[sizeof (Long) - 1] = 0;
  CHECK (LinuxBootCmdline (LINUX_DISTRO_ARMADAOS, Long, mPartUuid, &Out));
  CHECK (Out.Len == sizeof (Long));
  CHECK (Out.Data && AsciiStrEq ((CHAR8 *)Out.Data, Long));
  BufferFree (&Out);

  CHECK (LinuxBootCmdline (LINUX_DISTRO_PORTAREOS, "quiet", mPartUuid, &Out));
  CHECK (
    Out.Data &&
    AsciiStrEq (
      (CHAR8 *)Out.Data,
      "quiet portare.bootpart=PARTUUID=01234567-89ab-cdef-0123-456789abcdef"
    )
  );
  BufferFree (&Out);
  CHECK (LinuxBootCmdline (LINUX_DISTRO_PORTAREOS, "quiet", "", &Out));
  CHECK (Out.Data && AsciiStrEq ((CHAR8 *)Out.Data, "quiet"));
  BufferFree (&Out);
  CHECK (LinuxBootCmdline (LINUX_DISTRO_PORTAREOS, "", mPartUuid, &Out));
  CHECK (
    Out.Data &&
    AsciiStrEq (
      (CHAR8 *)Out.Data,
      "portare.bootpart=PARTUUID=01234567-89ab-cdef-0123-456789abcdef"
    )
  );
  BufferFree (&Out);
}

STATIC VOID
BootList (VOID)
{
  STATIC CONST ENTRY_VIEW Entries[] = {
    {.Title = "ArmadaOS", .Kind = ENTRY_KIND_BOOT_IMAGE},
    {.Title = "ROCKNIX", .Kind = ENTRY_KIND_BOOT_IMAGE, .Removable = TRUE},
    {.Title = "Android", .Kind = ENTRY_KIND_ANDROID},
  };
  APP       App;
  COMMAND   Cmd;
  ITEM_VIEW View;

  for (UINTN Index = 0; Index < 2; Index++) {
    AppInit (&App, BOOT_MODE_LINUX, TRUE, "test");
    AppSetEntries (&App, Entries, sizeof (Entries) / sizeof (*Entries));
    AppItemView (&App, ITEM_BOOT_ORDER, &View);
    CHECK (AsciiStrEq (View.Value, "Linux first"));
    AppInput (&App, INPUT_SELECT);
    CHECK (App.Screen == SCREEN_ENTRIES);
    for (UINTN Row = 0; Row < Index; Row++) {
      AppInput (&App, INPUT_DOWN);
    }
    Cmd = AppInput (&App, INPUT_SELECT);
    CHECK (Cmd.Kind == COMMAND_BOOT_ENTRY && Cmd.Arg == (INT32)Index);
    CHECK (App.Screen == SCREEN_BOOTING && App.Booting == (INTN)Index);
    CHECK (AsciiStrEq (App.Entries[Index].Title, Entries[Index].Title));
    AppOnBootResult (&App, BOOT_ERROR_PICK_FAILED, Entries[Index].Title);
    CHECK (App.Screen == SCREEN_ERROR);
    CHECK (AsciiStrEq (App.ErrorDetail, Entries[Index].Title));
  }
}

VOID
LinuxBootTests (VOID)
{
  CHECK (LinuxDistroOf ("PORTAREOS", FALSE) == LINUX_DISTRO_PORTAREOS);
  CHECK (LinuxDistroOf ("ARMADA", FALSE) == LINUX_DISTRO_ARMADAOS);
  CHECK (LinuxDistroOf ("ROCKNIX", TRUE) == LINUX_DISTRO_ARMADAOS);
  CHECK (LinuxDistroOf ("ROCKNIX", FALSE) == LINUX_DISTRO_ROCKNIX);
  CHECK (LinuxDistroOf (NULL, TRUE) == LINUX_DISTRO_ARMADAOS);
  CHECK (LinuxDistroOf (NULL, FALSE) == LINUX_DISTRO_UNKNOWN);
  CHECK (LinuxDistroOf ("OTHER", FALSE) == LINUX_DISTRO_UNKNOWN);
  CHECK (AsciiStrEq (LinuxDistroName (LINUX_DISTRO_PORTAREOS), "PortareOS"));
  CHECK (AsciiStrEq (LinuxDistroName (LINUX_DISTRO_ARMADAOS), "ArmadaOS"));
  CHECK (AsciiStrEq (LinuxDistroName (LINUX_DISTRO_ROCKNIX), "ROCKNIX"));
  CHECK (AsciiStrEq (LinuxDistroName (LINUX_DISTRO_UNKNOWN), "Linux"));
  CommandLines ();
  BootList ();
}
