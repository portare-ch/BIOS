/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  LinuxBoot - identifies Linux installations and keeps their boot arguments.
*/

#ifndef __CORE_LINUX_BOOT_H__
#define __CORE_LINUX_BOOT_H__

#include "Base/OablBase.h"
#include "Base/Buffer.h"

typedef enum {
  LINUX_DISTRO_UNKNOWN,
  LINUX_DISTRO_PORTAREOS,
  LINUX_DISTRO_ARMADAOS,
  LINUX_DISTRO_ROCKNIX,
} LINUX_DISTRO;

// Armada's boot-image stamp also identifies older cards labelled ROCKNIX.
LINUX_DISTRO
LinuxDistroOf (
  IN CONST CHAR8 *Label OPTIONAL,
  IN BOOLEAN      ArmadaStamp
);

CONST CHAR8 *
LinuxDistroName (IN LINUX_DISTRO Distro);

// Copies the complete image command line, including its extra_cmdline.
// Only PortareOS consumes portare.bootpart; other systems keep their own
// root/ostree/boot/disk arguments. PartUuid may be empty (e.g. fastboot).
// Out starts empty and is NUL-terminated on success. Caller frees it.
BOOLEAN
LinuxBootCmdline (
  IN LINUX_DISTRO  Distro,
  IN CONST CHAR8  *ImageCmdline,
  IN CONST CHAR8  *PartUuid,
  OUT OABL_BUFFER *Out
);

#endif
