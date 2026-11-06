/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

#include "Tests/Test.h"
#include "Ui/Ui.h"

VOID
BatteryTests (VOID)
{
  APP App;

  AppInit (&App, BOOT_MODE_LINUX, FALSE, "test");
  AppStartCharging (&App);
  AppOnBattery (&App, TRUE, TRUE, 4010);
  CHECK (AppBatteryLevel (&App) == 52);

  // The screen prints single percentages, including within one decile.
  App.Dirty = FALSE;
  AppOnBattery (&App, TRUE, TRUE, 4020);
  CHECK (AppBatteryLevel (&App) == 53);
  CHECK (App.Dirty);

  App.Dirty = FALSE;
  AppOnBattery (&App, TRUE, TRUE, 4021);
  CHECK (AppBatteryLevel (&App) == 53);
  CHECK (!App.Dirty);

  // Keep real voltage drops visible: charger presence is not proof that
  // the battery is gaining charge.
  AppOnBattery (&App, TRUE, TRUE, 4010);
  CHECK (AppBatteryLevel (&App) == 52);
  CHECK (App.Dirty);

  // A missing voltage hides the percentage, even if the last level was 0%.
  AppOnBattery (&App, TRUE, TRUE, 3460);
  CHECK (AppBatteryLevel (&App) == 0);
  App.Dirty = FALSE;
  AppOnBattery (&App, TRUE, TRUE, 0);
  CHECK (App.Dirty);
  App.Dirty = FALSE;
  AppOnBattery (&App, TRUE, TRUE, 3460);
  CHECK (AppBatteryLevel (&App) == 0);
  CHECK (App.Dirty);

  App.Dirty = FALSE;
  AppOnBattery (&App, TRUE, TRUE, 3460);
  CHECK (!App.Dirty);
  AppOnBattery (&App, TRUE, FALSE, 3460);
  CHECK (!App.Charger && App.Dirty);

  App.Dirty = FALSE;
  AppOnBattery (&App, FALSE, FALSE, 0);
  CHECK (!App.BatteryKnown && App.Dirty);
}
