/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Vbmeta - the vbmeta image Replace Android leaves for Android Verified
  Boot.

  Android's vbmeta chains vbmeta_system, which Replace Android removes, and
  libavb stops at a chained partition it can't read, even on an unlocked
  device. With VERIFICATION_DISABLED set, libavb follows no chain and only
  loads the partitions the ABL asks for. The image is libavb's 256-byte
  header (avb_vbmeta_image.h, big-endian) with no signature, descriptors
  or other blocks.
*/

#ifndef __CORE_VBMETA_H__
#define __CORE_VBMETA_H__

#include "Base/OablBase.h"

#define VBMETA_HEADER_SIZE 256

// AvbVBMetaImageFlags.
#define VBMETA_FLAG_HASHTREE_DISABLED     1u
#define VBMETA_FLAG_VERIFICATION_DISABLED 2u

// Writes the header of an unsigned vbmeta image with VERIFICATION_DISABLED
// set, for libavb 1.0 and later.
VOID
VbmetaDisabled (OUT UINT8 Out[VBMETA_HEADER_SIZE]);

#endif
