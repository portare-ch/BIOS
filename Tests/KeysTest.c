/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

#include "Tests/Test.h"
#include "Ui/Ui.h"

// Feeds Key every Step ms from From up to but not including To, as a held
// key reports after each reset of the input. Returns the inputs it made.
STATIC UINTN
Hold (
  IN OUT KEYS *Keys,
  IN INPUT     Key,
  IN UINT64    From,
  IN UINT64    To,
  IN UINT64    Step
)
{
  UINTN Made = 0;
  INPUT Out;

  for (UINT64 Now = From; Now < To; Now += Step) {
    Made += KeysPress (Keys, Key, Now, &Out) ? 1 : 0;
  }
  return Made;
}

VOID
KeysTests (VOID)
{
  KEYS  Keys = {0};
  INPUT Out  = INPUT_UP;

  // A held Down moves once, then repeats after the delay.
  CHECK (KeysPress (&Keys, INPUT_DOWN, 1000, &Out) && Out == INPUT_DOWN);
  CHECK (Hold (&Keys, INPUT_DOWN, 1020, 1400, 20) == 0);
  CHECK (KeysPress (&Keys, INPUT_DOWN, 1400, &Out) && Out == INPUT_DOWN);
  CHECK (Hold (&Keys, INPUT_DOWN, 1420, 1520, 20) == 0);
  CHECK (KeysPress (&Keys, INPUT_DOWN, 1520, &Out));
  // Released: a poll without a report, a gap after the last one.
  KeysIdle (&Keys, 1600);
  CHECK (Keys.Held);
  KeysIdle (&Keys, 1720);
  CHECK (!Keys.Held);

  // A held Select acts once, however long it is held.
  CHECK (KeysPress (&Keys, INPUT_SELECT, 2000, &Out) && Out == INPUT_SELECT);
  CHECK (Hold (&Keys, INPUT_SELECT, 2020, 4000, 20) == 0);
  KeysIdle (&Keys, 4300);
  CHECK (!Keys.Held);

  // A key already down when the menu opens makes no input, however late
  // its next report comes, and doesn't scroll.
  KeysMute (&Keys, INPUT_DOWN, 5000);
  CHECK (!KeysPress (&Keys, INPUT_DOWN, 5900, &Out));
  CHECK (Hold (&Keys, INPUT_DOWN, 5920, 7000, 20) == 0);
  // An idle poll soon after a report doesn't release it.
  KeysIdle (&Keys, 7050);
  CHECK (Keys.Held && Keys.Muted);
  CHECK (!KeysPress (&Keys, INPUT_DOWN, 7060, &Out));
  // Once released it acts again.
  KeysIdle (&Keys, 7300);
  CHECK (!Keys.Held);
  CHECK (KeysPress (&Keys, INPUT_DOWN, 7400, &Out) && Out == INPUT_DOWN);
  CHECK (!Keys.Muted);

  // Another key pressed while one is muted acts at once.
  KeysMute (&Keys, INPUT_DOWN, 8000);
  CHECK (KeysPress (&Keys, INPUT_SELECT, 8050, &Out) && Out == INPUT_SELECT);
}
