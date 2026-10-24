/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Qcom - calls into Qualcomm's XBL protocols, made as LinuxLoader makes them.
*/

#ifndef __EFI_QCOM_H__
#define __EFI_QCOM_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

// Commits NV variables to storage with FlushVariableNV, as a variable that
// isn't flushed may be lost at the next reset. FALSE without the
// protocol.
BOOLEAN
QcomFlushVariables (OUT EFI_STATUS *Status);

#endif
