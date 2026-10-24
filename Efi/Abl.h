/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Abl - the libraries the ABL shares with us.
*/

#ifndef __EFI_ABL_H__
#define __EFI_ABL_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"

// AblLib: OPENABL_ABL_LIB_PROTOCOL (Protocol/OpenAblAblLib.h in the ABL)
// gives us Qualcomm's decompress for gzip kernels and libfdt for the
// devicetree editor. The libfdt functions in Core/LibFdt.h are defined here
// under their own names and forward to the ABL. Without the protocol they
// return -FDT_ERR_BADSTATE.

// Locates the ABL's library protocol. Call it before any devicetree edit or
// inflate. Returns FALSE if the ABL doesn't have it.
BOOLEAN
AblLibInit (VOID);

// Inflates the gzip stream In into Out with the ABL's decompress. OutSize
// must be larger than InSize and hold the whole output. Written gets the
// output size, Used the input consumed including the 8-byte trailer. Only
// the file name field of the gzip header is skipped. Returns EFI_LOAD_ERROR
// on failure, EFI_UNSUPPORTED without the protocol.
EFI_STATUS
AblLibInflate (
  IN CONST VOID *In,
  IN UINTN       InSize,
  OUT VOID      *Out,
  IN UINTN       OutSize,
  OUT UINTN     *Written,
  OUT UINTN     *Used
);

#endif
