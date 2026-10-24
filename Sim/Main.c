/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Main - renders the menu's screens to PNG on the host, on the Nova's
  960 x 1280 panel turned 90 degrees.

    openabl-menu-sim [--out DIR] [--filter TEXT] [--check]

  It writes each screen as the user sees it (NAME.png), and the main menu
  also as the panel receives it (menu-scanout.png). --check instead runs
  sessions of key presses and compares each frame with a full redraw and
  with a freshly built view.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "Base/Text.h"
#include "Base/Memory.h"
#include "Ui/Lang.h"
#include "Ui/Ui.h"
#include "View/View.h"

// The Nova's panel, and how far the menu turns its picture.
#define PANEL_W  960
#define PANEL_H  1280
#define ROTATION 90

STATIC UINT32  mClockMs;
STATIC UINT32 *mLogical, *mPhysical;
STATIC INT32   mPw;

// Simulated time, in milliseconds.
STATIC UINT32
Tick (VOID)
{
  return mClockMs;
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
  for (UINT32 Row = 0; Row < H; Row++) {
    CopyMem (
      &mPhysical[(UINTN)(Y + Row) * mPw + X], &Px[(UINTN)Row * Stride],
      (UINTN)W * 4
    );
  }
}

STATIC VOID
Chunk (
  IN OUT FILE    *File,
  IN CONST CHAR8 *Type,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINT32       Len
)
{
  UINT8 Length[4] = {Len >> 24, Len >> 16, Len >> 8, Len};
  fwrite (Length, 1, 4, File);
  fwrite (Type, 1, 4, File);
  fwrite (Data, 1, Len, File);
  uLong Crc         = crc32 (crc32 (0, (CONST Bytef *)Type, 4), Data, Len);
  UINT8 CrcBytes[4] = {Crc >> 24, Crc >> 16, Crc >> 8, Crc};
  fwrite (CrcBytes, 1, 4, File);
}

// Writes 0x00RRGGBB pixels as an RGB PNG. Exits if Path can't be opened.
STATIC VOID
WritePng (
  IN CONST CHAR8  *Path,
  IN INT32         W,
  IN INT32         H,
  IN CONST UINT32 *Px
)
{
  UINTN  RowSize       = 1 + (UINTN)W * 3;
  UINTN  RawLen        = RowSize * H;
  UINT8 *Raw           = malloc (RawLen);
  uLongf CompressedLen = compressBound (RawLen);
  UINT8 *Compressed    = malloc (CompressedLen);

  for (INT32 Y = 0; Y < H; Y++) {
    UINT8 *Out = Raw + RowSize * Y;
    *Out++     = 0;
    for (INT32 X = 0; X < W; X++) {
      UINT32 Pixel = Px[(UINTN)Y * W + X];
      *Out++       = Pixel >> 16;
      *Out++       = Pixel >> 8;
      *Out++       = Pixel;
    }
  }
  compress2 (Compressed, &CompressedLen, Raw, RawLen, 6);
  FILE *File = fopen (Path, "wb");
  if (!File) {
    perror (Path);
    exit (1);
  }
  STATIC CONST UINT8 Signature[8] = {0x89, 'P',  'N',  'G',
                                     '\r', '\n', 0x1a, '\n'};
  UINT8 Ihdr[13] = {W >> 24, W >> 16, W >> 8, W, H >> 24, H >> 16, H >> 8,
                    H,       8,       2,      0, 0,       0};
  fwrite (Signature, 1, 8, File);
  Chunk (File, "IHDR", Ihdr, 13);
  Chunk (File, "IDAT", Compressed, (UINT32)CompressedLen);
  Chunk (File, "IEND", NULL, 0);
  fclose (File);
  free (Raw);
  free (Compressed);
}

// The boot list on a Nova with PortareOS inside and a PortareOS card.
STATIC CONST ENTRY_VIEW mEntries[] = {
  {.Title = "PortareOS", .Kind = ENTRY_KIND_BOOT_IMAGE},
  {.Title = "PortareOS", .Kind = ENTRY_KIND_BOOT_IMAGE, .Removable = TRUE},
  {.Title = "Android", .Kind = ENTRY_KIND_ANDROID},
};

// A stock Nova (Android alone, userdata from 14520 MiB of 117776) with a
// PortareOS card in.
#define MIB (1ull << 20)
STATIC CONST INSTALL_INFO mInstall = {
  .Card           = TRUE,
  .Known          = TRUE,
  .Android        = TRUE,
  .Dualboot       = TRUE,
  .Replace        = TRUE,
  .Shared         = 103255 * MIB,
  .AndroidMin     = 4ull << 30,
  .AndroidMax     = 91ull << 30,
  .BootBytes      = 2048 * MIB,
  .UpdateStorage  = 100183 * MIB,
  .ReplaceStorage = 113496 * MIB,
};

// Screens

STATIC VOID
MakeApp (OUT APP *A)
{
  AppInit (A, BOOT_MODE_LINUX, TRUE, MENU_VERSION);
  AppSetEntries (A, mEntries, sizeof (mEntries) / sizeof (mEntries[0]));
  AppSetInstall (A, &mInstall);
}

// Takes A, started at Now, to one of the screens.
typedef VOID (*SESSION_SETUP) (
  IN OUT APP *A,
  IN UINT64   Now
);

STATIC VOID
SMenu (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)A, (VOID)Now;
}

// After Replace Android: only PortareOS starts.
STATIC VOID
SNoAndroid (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppSetAndroid (A, FALSE);
}

// What Boot order does, and the CHANGE it takes.
STATIC VOID
SBootOrder (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_DOWN);
}

// Emergency download mode, with Enter EDL picked.
STATIC VOID
SConfirmEdl (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  for (INT32 Index = 0; Index < 3; Index++) {
    AppInput (A, INPUT_DOWN);
  }
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

// Boot opens the boot list.
STATIC VOID
SBootList (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_SELECT);
}

// The boot list on PortareOS from the SD card.
STATIC VOID
SBootListSd (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SBooting (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SBootingSd (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootListSd (A, Now);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SBootingAndroid (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SBootList (A, Now);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SErrorNoImage (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnBootResult (A, BOOT_ERROR_NO_IMAGE, NULL);
}

// Verified boot refused PortareOS's boot image.
STATIC VOID
SErrorLoad (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnBootResult (A, BOOT_ERROR_LOAD_FAILED, gStrings->ErrBootImageFailed);
}

STATIC VOID
SToast (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnFailed (A, gStrings->AblSettingFailed);
}

// The SD card was taken out while its PortareOS was picked.
STATIC VOID
SErrorPickGone (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnPickFailed (A, TRUE, "PortareOS", TRUE);
}

// PortareOS on the SD card was there but didn't start.
STATIC VOID
SErrorPickFailed (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppOnPickFailed (A, FALSE, "PortareOS", TRUE);
}

// Install PortareOS's choices, Dualboot focused.
STATIC VOID
SInstall (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
}

STATIC VOID
SInstallReplace (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstall (A, Now);
  AppInput (A, INPUT_DOWN);
}

// PortareOS next to Android already: Dualboot updates it.
STATIC VOID
SInstallUpdate (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Update    = TRUE;
  Info.PortareOs = TRUE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

// Partitions BIOS doesn't know: neither choice runs.
STATIC VOID
SInstallUnknown (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Known    = FALSE;
  Info.Dualboot = FALSE;
  Info.Replace  = FALSE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

STATIC VOID
SInstallSize (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstall (A, Now);
  AppInput (A, INPUT_SELECT);
}

// Android keeping 16 GB, with Install picked.
STATIC VOID
SConfirmDualboot (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallSize (A, Now);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SConfirmUpdate (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallUpdate (A, Now);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

// Replace Android over a Dualboot install.
STATIC VOID
SConfirmReplace (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstallUpdate (A, Now);
  AppInput (A, INPUT_DOWN);
  AppInput (A, INPUT_SELECT);
  AppInput (A, INPUT_DOWN);
}

STATIC VOID
SInstalling (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SConfirmDualboot (A, Now);
  AppInput (A, INPUT_SELECT);
  AppOnInstallProgress (A, INSTALL_STAGE_COPY, 437);
}

STATIC VOID
SInstalled (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  SInstalling (A, Now);
  AppOnInstalled (A, TRUE, NULL, TRUE);
}

STATIC VOID
SErrorNoCard (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  INSTALL_INFO Info = mInstall;

  Info.Card = FALSE;
  AppSetInstall (A, &Info);
  SInstall (A, Now);
}

// A write failed after the table changed.
STATIC VOID
SErrorInstall (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  CONST CHAR8 *Args[1] = {"error 7"};
  CHAR8        Why[192];

  SInstalling (A, Now);
  LangFill (Why, sizeof (Why), gStrings->InstallWriteFailed, Args, 1);
  AppOnInstalled (A, FALSE, Why, TRUE);
}

// Powered on by the charger, about half charged.
STATIC VOID
SCharging (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 4010);
}

STATIC VOID
SChargingLow (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 3690);
}

// XBL couldn't read the battery's voltage.
STATIC VOID
SChargingUnknown (
  IN OUT APP *A,
  IN UINT64   Now
)
{
  (VOID)Now;
  AppStartCharging (A);
  AppOnBattery (A, TRUE, TRUE, 0);
}

STATIC CONST struct {
  CONST CHAR8  *Name;
  SESSION_SETUP Setup;
} mScreens[] = {
  {"menu", SMenu},
  {"menu-no-android", SNoAndroid},
  {"menu-boot-order", SBootOrder},
  {"boot-list", SBootList},
  {"boot-list-sd", SBootListSd},
  {"confirm-edl", SConfirmEdl},
  {"install", SInstall},
  {"install-replace", SInstallReplace},
  {"install-update", SInstallUpdate},
  {"install-unknown", SInstallUnknown},
  {"install-size", SInstallSize},
  {"confirm-dualboot", SConfirmDualboot},
  {"confirm-update", SConfirmUpdate},
  {"confirm-replace", SConfirmReplace},
  {"installing", SInstalling},
  {"installed", SInstalled},
  {"error-no-card", SErrorNoCard},
  {"error-install", SErrorInstall},
  {"charging", SCharging},
  {"charging-low", SChargingLow},
  {"charging-unknown", SChargingUnknown},
  {"booting", SBooting},
  {"booting-sd", SBootingSd},
  {"booting-android", SBootingAndroid},
  {"error-no-image", SErrorNoImage},
  {"error-load", SErrorLoad},
  {"error-pick-gone", SErrorPickGone},
  {"error-pick-failed", SErrorPickFailed},
  {"toast", SToast},
};

STATIC APP mApp;

// Renders mScreens[Screen] to a PNG in Out. With Scanout it also writes
// what the panel receives.
STATIC VOID
Render (
  IN CONST CHAR8 *Out,
  IN UINTN        Screen,
  IN BOOLEAN      Scanout
)
{
  UINT64 Now = 10000;
  CHAR8  Path[512];

  MakeApp (&mApp);
  AppTick (&mApp, Now);
  mScreens[Screen].Setup (&mApp, Now);

  mPw              = PANEL_W;
  UINTN PixelCount = (UINTN)PANEL_W * PANEL_H;
  mLogical         = calloc (PixelCount, 4);
  mPhysical        = calloc (PixelCount, 4);
  PANEL Panel      = {.W       = PANEL_W,
                      .H       = PANEL_H,
                      .Present = BlitToPanel,
                      .Now     = Tick,
                      .Logical = mLogical};
  if (!ViewInit (&Panel)) {
    fprintf (stderr, "the view didn't start\n");
    exit (1);
  }
  ViewRotate (ROTATION);
  UINT32 LogicalW;
  UINT32 LogicalH;
  ViewLogicalSize (&LogicalW, &LogicalH);

  ViewSync (&mApp);
  // Let the fades, scrolls and sweeps run a little.
  for (INT32 Index = 0; Index < 12; Index++) {
    mClockMs += 50;
    ViewRun ();
  }
  ViewSettle ();

  snprintf (Path, sizeof (Path), "%s/%s.png", Out, mScreens[Screen].Name);
  WritePng (Path, (INT32)LogicalW, (INT32)LogicalH, mLogical);
  if (Scanout) {
    snprintf (
      Path, sizeof (Path), "%s/%s-scanout.png", Out, mScreens[Screen].Name
    );
    WritePng (Path, PANEL_W, PANEL_H, mPhysical);
  }
  ViewDone ();
  free (mLogical);
  free (mPhysical);
}

// --check runs sessions of key presses and platform replies the way the
// main loop does: the view synced when the app changes, timers every 10 ms.
// After every step the panel is compared with a full redraw. A difference
// means the screen shows something the app's state doesn't explain, such as
// a leftover where an object was.

// NOTIFY shows a toast, BOOT_ERROR a boot that found nothing. FRESH
// compares the screen with a view built fresh for the same state.
typedef enum {
  STEP_KEY,
  STEP_FAST,
  STEP_NOTIFY,
  STEP_WAIT,
  STEP_BOOT_ERROR,
  STEP_FRESH,
} STEP_KIND;

typedef struct _STEP {
  STEP_KIND Kind;
  INPUT     Key;
  INT32     Value; // repeats, or WAIT ms
} STEP;

#define PRESS(Key, Count) {STEP_KEY, INPUT_##Key, Count}
// Presses 40 ms apart, like a held key, so each lands mid-animation.
#define PRESS_FAST(Key, Count) {STEP_FAST, INPUT_##Key, Count}
#define FRESH                  {STEP_FRESH, 0, 0}

STATIC CONST STEP mSessionMenu[] = {PRESS (DOWN, 24),
                                    PRESS (UP, 24),
                                    PRESS (DOWN, 6),
                                    {STEP_WAIT, 0, 400},
                                    PRESS (UP, 6)};
// Install PortareOS's choices and back, then Emergency download mode and
// back.
STATIC CONST STEP mSessionConfirm[] = {
  PRESS (DOWN, 2),   PRESS (SELECT, 1), PRESS (BACK, 1), PRESS (DOWN, 1),
  PRESS (SELECT, 1), PRESS (DOWN, 1),   PRESS (UP, 1),   PRESS (BACK, 1)};
STATIC CONST STEP mSessionToast[] = {{STEP_NOTIFY, 0, 1}, {STEP_WAIT, 0, 3500},
                                     PRESS (DOWN, 3),     {STEP_NOTIFY, 0, 1},
                                     PRESS (UP, 3),       {STEP_WAIT, 0, 3500}};
STATIC CONST STEP mSessionHeld[] = {PRESS_FAST (DOWN, 20), {STEP_WAIT, 0, 400},
                                    PRESS_FAST (UP, 20),   {STEP_WAIT, 0, 400},
                                    PRESS_FAST (DOWN, 7),  PRESS_FAST (UP, 3),
                                    PRESS_FAST (DOWN, 9),  {STEP_WAIT, 0, 600}};

// The boot list, an error and a toast, each against a fresh view.
STATIC CONST STEP mSessionPaths[] = {FRESH,
                                     PRESS (SELECT, 1),
                                     FRESH,
                                     PRESS (DOWN, 2),
                                     FRESH,
                                     PRESS_FAST (DOWN, 5),
                                     FRESH,
                                     PRESS (BACK, 1),
                                     FRESH,
                                     {STEP_BOOT_ERROR, 0, 1},
                                     FRESH,
                                     PRESS (SELECT, 1),
                                     FRESH,
                                     {STEP_NOTIFY, 0, 1},
                                     {STEP_WAIT, 0, 300},
                                     FRESH,
                                     PRESS (DOWN, 1),
                                     FRESH};

// Install PortareOS's screens, each against a fresh view.
STATIC CONST STEP mSessionInstall[] = {
  FRESH,           PRESS (DOWN, 2),   PRESS (SELECT, 1), FRESH,
  PRESS (DOWN, 1), FRESH,             PRESS (UP, 1),     PRESS (SELECT, 1),
  FRESH,           PRESS (DOWN, 3),   FRESH,             PRESS (UP, 2),
  PRESS (SELECT, 1), FRESH,           PRESS (DOWN, 1),   FRESH,
  PRESS (BACK, 1), FRESH,             PRESS (DOWN, 1),   PRESS (SELECT, 1),
  FRESH,           PRESS (BACK, 1),   FRESH,             PRESS (BACK, 1),
  FRESH};

STATIC CONST struct {
  CONST CHAR8 *Name;
  CONST STEP  *Steps;
  UINTN        Count;
} mSessions[] = {
#define SESSION(Name, Steps) {Name, Steps, sizeof (Steps) / sizeof (Steps[0])}
  SESSION ("menu", mSessionMenu),
  SESSION ("confirm", mSessionConfirm),
  SESSION ("install", mSessionInstall),
  SESSION ("toast", mSessionToast),
  SESSION ("held", mSessionHeld),
  SESSION ("paths", mSessionPaths),
#undef SESSION
};

STATIC UINT32 *mSnap;
STATIC INT32   mBadFrames, mWritten;

// Compares mSnap with mPhysical (a full redraw, or with Fresh a fresh
// view). The first 12 mismatches are also saved as PNGs.
STATIC VOID
CompareFrame (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H,
  IN BOOLEAN      Fresh
)
{
  UINTN Diff = 0;
  INT32 X0   = W;
  INT32 Y0   = H;
  INT32 X1   = -1;
  INT32 Y1   = -1;

  for (INT32 Y = 0; Y < H; Y++) {
    for (INT32 X = 0; X < W; X++) {
      UINTN Index = (UINTN)Y * W + X;
      if ((mSnap[Index] & 0xffffff) != (mPhysical[Index] & 0xffffff)) {
        Diff++;
        if (X < X0) {
          X0 = X;
        }
        if (Y < Y0) {
          Y0 = Y;
        }
        if (X > X1) {
          X1 = X;
        }
        if (Y > Y1) {
          Y1 = Y;
        }
      }
    }
  }
  if (!Diff) {
    return;
  }
  mBadFrames++;
  printf (
    "  %s %s %d: %zu pixels differ%s in (%d,%d)-(%d,%d)\n", What,
    Fresh ? "at frame" : "frame", Frame, Diff,
    Fresh ? " from a fresh view" : "", X0, Y0, X1, Y1
  );
  if (mWritten < 12) {
    CHAR8 Path[512];
    snprintf (
      Path, sizeof (Path), "%s/check-%02d-%s-%d-%s.png", Out, mWritten, What,
      Frame, Fresh ? "way" : "drawn"
    );
    WritePng (Path, W, H, mSnap);
    snprintf (
      Path, sizeof (Path), "%s/check-%02d-%s-%d-%s.png", Out, mWritten, What,
      Frame, Fresh ? "fresh" : "redrawn"
    );
    WritePng (Path, W, H, mPhysical);
    mWritten++;
  }
}

// Compares the panel with a full redraw.
STATIC VOID
CheckFrame (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H
)
{
  // Flush what the view has already asked to redraw. A change it hasn't
  // flushed yet belongs to the next frame and isn't a leftover.
  ViewRedraw (FALSE);
  CopyMem (mSnap, mPhysical, (UINTN)W * H * 4);
  ViewRedraw (TRUE);
  CompareFrame (Out, What, Frame, W, H, FALSE);
}

STATIC PANEL mCheckPanel;

// Compares the screen as the session left it with a view built fresh for
// the same state. The fresh view carries on.
STATIC VOID
FreshCheck (
  IN CONST CHAR8 *Out,
  IN CONST CHAR8 *What,
  IN INT32        Frame,
  IN INT32        W,
  IN INT32        H
)
{
  UINTN PixelCount = (UINTN)W * H;

  if (mApp.Dirty) {
    ViewSync (&mApp);
  }
  ViewRun ();
  ViewSettle ();
  ViewRedraw (FALSE);
  CopyMem (mSnap, mPhysical, PixelCount * 4);
  ViewDone ();
  ZeroMem (mPhysical, PixelCount * 4);
  if (!ViewInit (&mCheckPanel)) {
    fprintf (stderr, "the view didn't start again\n");
    exit (1);
  }
  ViewRotate (ROTATION);
  ViewSync (&mApp);
  ViewSettle ();
  ViewRedraw (TRUE);
  CompareFrame (Out, What, Frame, W, H, TRUE);
}

// Runs mSessions[Session] and checks every frame. Mismatches go to Out.
STATIC VOID
Check (
  IN CONST CHAR8 *Out,
  IN UINTN        Session
)
{
  UINT64 Now   = 10000;
  INT32  Frame = 0;

  MakeApp (&mApp);
  AppTick (&mApp, Now);
  mPw              = PANEL_W;
  UINTN PixelCount = (UINTN)PANEL_W * PANEL_H;
  mLogical         = calloc (PixelCount, 4);
  mPhysical        = calloc (PixelCount, 4);
  mSnap            = calloc (PixelCount, 4);
  mClockMs         = (UINT32)Now;
  mCheckPanel      = (PANEL){.W       = PANEL_W,
                             .H       = PANEL_H,
                             .Present = BlitToPanel,
                             .Now     = Tick,
                             .Logical = mLogical};
  if (!ViewInit (&mCheckPanel)) {
    fprintf (stderr, "the view didn't start\n");
    exit (1);
  }
  ViewRotate (ROTATION);
  mApp.Dirty = TRUE;
  for (UINTN Index = 0; Index < mSessions[Session].Count; Index++) {
    CONST STEP *Step = &mSessions[Session].Steps[Index];
    INT32       Repeats =
      Step->Kind == STEP_KEY || Step->Kind == STEP_FAST ? Step->Value : 1;
    INT32 Ms = Step->Kind == STEP_WAIT   ? Step->Value
               : Step->Kind == STEP_FAST ? 40
                                         : 300;
    if (Step->Kind == STEP_FRESH) {
      FreshCheck (Out, mSessions[Session].Name, Frame, PANEL_W, PANEL_H);
      continue;
    }
    for (INT32 Repeat = 0; Repeat < Repeats; Repeat++) {
      switch (Step->Kind) {
      case STEP_KEY:
      case STEP_FAST:
        AppInput (&mApp, Step->Key);
        break;
      case STEP_NOTIFY:
        AppOnFailed (&mApp, gStrings->AblSettingFailed);
        break;
      case STEP_BOOT_ERROR:
        AppOnBootResult (&mApp, BOOT_ERROR_NO_IMAGE, NULL);
        break;
      case STEP_WAIT:
      case STEP_FRESH:
        break;
      }
      // The main loop, every 10 ms.
      for (INT32 Elapsed = 0; Elapsed < Ms; Elapsed += 10) {
        mClockMs += 10;
        AppTick (&mApp, mClockMs);
        if (mApp.Dirty) {
          ViewSync (&mApp);
        }
        ViewRun ();
        CheckFrame (Out, mSessions[Session].Name, Frame++, PANEL_W, PANEL_H);
      }
    }
  }
  ViewDone ();
  free (mLogical);
  free (mPhysical);
  free (mSnap);
}

INT32
main (
  INT32   Argc,
  CHAR8 **Argv
)
{
  CONST CHAR8 *Out = "build/screens", *Filter = "";
  BOOLEAN      DoCheck = FALSE;
  INT32        Count   = 0;

  for (INT32 Index = 1; Index < Argc; Index++) {
    if (!strcmp (Argv[Index], "--out") && Index + 1 < Argc) {
      Out = Argv[++Index];
    } else if (!strcmp (Argv[Index], "--filter") && Index + 1 < Argc) {
      Filter = Argv[++Index];
    } else if (!strcmp (Argv[Index], "--check")) {
      DoCheck = TRUE;
    } else {
      fprintf (
        stderr,
        "usage: %s [--out DIR] [--filter TEXT] [--check]\n",
        Argv[0]
      );
      return 2;
    }
  }
  if (DoCheck) {
    for (UINTN Session = 0;
         Session < sizeof (mSessions) / sizeof (mSessions[0]); Session++) {
      INT32 Before = mBadFrames;

      if (!strstr (mSessions[Session].Name, Filter)) {
        continue;
      }
      Check (Out, Session);
      printf (
        "%s: %s\n", mSessions[Session].Name,
        mBadFrames == Before ? "clean" : "MISMATCH"
      );
      Count++;
    }
    printf (
      "checked %d sessions: %d frame(s) differ from a full redraw\n", Count,
      mBadFrames
    );
    return mBadFrames ? 1 : 0;
  }
  for (UINTN Screen = 0; Screen < sizeof (mScreens) / sizeof (mScreens[0]);
       Screen++) {
    if (!strstr (mScreens[Screen].Name, Filter)) {
      continue;
    }
    Render (Out, Screen, Screen == 0);
    Count++;
  }
  printf ("rendered %d screens to %s\n", Count, Out);
  return 0;
}
