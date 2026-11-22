/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

#include "Core/BootDecision.h"
#include "Tests/Test.h"

VOID
BootDecisionTests (VOID)
{
  BOOT_DECISION_START Plain    = {0};
  BOOT_DECISION_START Recovery = {.Recovery = TRUE};
  // Qualcomm sets its recovery flag for Volume Up too.
  BOOT_DECISION_START Up   = {.Key      = BOOT_DECISION_SCAN_UP,
                              .Recovery = TRUE};
  BOOT_DECISION_START Home = {.Key = BOOT_DECISION_SCAN_HOME};
  BOOT_DECISION_START Fastboot = {.Fastboot = TRUE, .Recovery = TRUE};

  // Nothing held: the Boot order decides.
  CHECK (!BootDecisionAndroid (&Plain, FALSE));
  CHECK (BootDecisionAndroid (&Plain, TRUE));
  CHECK (!BootDecisionRecovery (&Plain, FALSE));
  CHECK (!BootDecisionRecovery (&Plain, TRUE));

  // A restart into recovery starts Android's recovery, whichever is first.
  CHECK (BootDecisionAndroid (&Recovery, FALSE));
  CHECK (BootDecisionRecovery (&Recovery, FALSE));
  CHECK (BootDecisionAndroid (&Recovery, TRUE));
  CHECK (BootDecisionRecovery (&Recovery, TRUE));

  // Volume Up: Android itself with Linux first, recovery with Android first.
  CHECK (BootDecisionAndroid (&Up, FALSE));
  CHECK (!BootDecisionRecovery (&Up, FALSE));
  CHECK (BootDecisionAndroid (&Up, TRUE));
  CHECK (BootDecisionRecovery (&Up, TRUE));

  // Home: Android with Linux first, recovery with Android first.
  CHECK (BootDecisionAndroid (&Home, FALSE));
  CHECK (!BootDecisionRecovery (&Home, FALSE));
  CHECK (BootDecisionRecovery (&Home, TRUE));

  // Fastboot comes before either.
  CHECK (BootDecisionFastboot (&Fastboot));
  CHECK (!BootDecisionFastboot (&Recovery));
}
