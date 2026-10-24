/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 * Copyright (C) 2026-present PortareOS
 *   https://portare.org
 */

/**
  Abl - the libraries the ABL shares with us (Efi/Abl.h).
*/

#include "Efi/Abl.h"

#include <Protocol/OpenAblAblLib.h>

#include "Core/LibFdt.h"

// ABL library protocol

STATIC EFI_GUID mAblLibGuid = OPENABL_ABL_LIB_PROTOCOL_GUID;

STATIC OPENABL_ABL_LIB_PROTOCOL *mLib;

// What the libfdt wrappers return without the protocol.
#define NO_LIB (-FDT_ERR_BADSTATE)

BOOLEAN
AblLibInit (VOID)
{
  mLib = LocateProtocol (&mAblLibGuid);
  return mLib != NULL;
}

EFI_STATUS
AblLibInflate (
  IN CONST VOID *In,
  IN UINTN       InSize,
  OUT VOID      *Out,
  IN UINTN       OutSize,
  OUT UINTN     *Written,
  OUT UINTN     *Used
)
{
  UINTN      Produced = 0;
  UINTN      Consumed = 0;
  EFI_STATUS Status;

  if (!mLib) {
    return EFI_UNSUPPORTED;
  }
  Status = mLib->Inflate (mLib, In, InSize, Out, OutSize, &Produced, &Consumed);
  *Written = Produced;
  *Used    = Consumed;
  return Status;
}

// libfdt, forwarded to the ABL.

INT32
fdt_path_offset (
  IN CONST VOID  *Fdt,
  IN CONST CHAR8 *Path
)
{
  return mLib ? mLib->fdt_path_offset (Fdt, Path) : NO_LIB;
}

CONST CHAR8 *
fdt_get_name (
  IN CONST VOID *Fdt,
  IN INT32       Nodeoffset,
  OUT INT32     *Lenp OPTIONAL
)
{
  if (mLib) {
    return mLib->fdt_get_name (Fdt, Nodeoffset, Lenp);
  }
  if (Lenp) {
    *Lenp = NO_LIB;
  }
  return NULL;
}

CONST VOID *
fdt_getprop (
  IN CONST VOID  *Fdt,
  IN INT32        Nodeoffset,
  IN CONST CHAR8 *Name,
  OUT INT32      *Lenp OPTIONAL
)
{
  if (mLib) {
    return mLib->fdt_getprop (Fdt, Nodeoffset, Name, Lenp);
  }
  if (Lenp) {
    *Lenp = NO_LIB;
  }
  return NULL;
}

INT32
fdt_next_node (
  IN CONST VOID *Fdt,
  IN INT32       Offset,
  IN OUT INT32  *Depth OPTIONAL
)
{
  return mLib ? mLib->fdt_next_node (Fdt, Offset, Depth) : NO_LIB;
}

INT32
fdt_first_subnode (
  IN CONST VOID *Fdt,
  IN INT32       Offset
)
{
  return mLib ? mLib->fdt_first_subnode (Fdt, Offset) : NO_LIB;
}

INT32
fdt_next_subnode (
  IN CONST VOID *Fdt,
  IN INT32       Offset
)
{
  return mLib ? mLib->fdt_next_subnode (Fdt, Offset) : NO_LIB;
}

INT32
fdt_node_offset_by_compatible (
  IN CONST VOID  *Fdt,
  IN INT32        Startoffset,
  IN CONST CHAR8 *Compatible
)
{
  return mLib
           ? mLib->fdt_node_offset_by_compatible (Fdt, Startoffset, Compatible)
           : NO_LIB;
}

INT32
fdt_setprop (
  IN OUT VOID    *Fdt,
  IN INT32        Nodeoffset,
  IN CONST CHAR8 *Name,
  IN CONST VOID  *Val,
  IN INT32        Len
)
{
  return mLib ? mLib->fdt_setprop (Fdt, Nodeoffset, Name, Val, Len) : NO_LIB;
}

INT32
fdt_add_subnode (
  IN OUT VOID    *Fdt,
  IN INT32        Parentoffset,
  IN CONST CHAR8 *Name
)
{
  return mLib ? mLib->fdt_add_subnode (Fdt, Parentoffset, Name) : NO_LIB;
}
