/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Vbmeta - the vbmeta image Replace Android leaves (Core/Vbmeta.h).
*/

#include "Core/Vbmeta.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"

// AvbVBMetaImageHeader offsets.
#define AT_VERSION_MAJOR 4
#define AT_FLAGS         120
#define AT_RELEASE       128
#define RELEASE_SIZE     48

VOID
VbmetaDisabled (OUT UINT8 Out[VBMETA_HEADER_SIZE])
{
  STATIC CONST CHAR8 Release[] = "Portare BIOS";

  // No blocks and algorithm NONE: every size, offset and the rollback
  // index are 0, and libavb 1.0 reads it.
  ZeroMem (Out, VBMETA_HEADER_SIZE);
  CopyMem (Out, "AVB0", 4);
  WriteBe32 (Out + AT_VERSION_MAJOR, 1);
  WriteBe32 (Out + AT_FLAGS, VBMETA_FLAG_VERIFICATION_DISABLED);
  _Static_assert (sizeof (Release) < RELEASE_SIZE, "release string");
  CopyMem (Out + AT_RELEASE, Release, sizeof (Release) - 1);
}
