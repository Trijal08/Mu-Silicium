/*++

Copyright The Asahi Linux Contributors
Copyright (c) 2026, Trijal Saha. All rights reserved.<BR>

Based on the Linux irq-apple-aic driver:
  Copyright The Asahi Linux Contributors

SPDX-License-Identifier: GPL-2.0-or-later

Module Name:

  AppleAicDxe.c

Abstract:

  Driver implementing the Apple Interrupt Controller (AIC) protocol

--*/

#include <PiDxe.h>

#include "AppleAicDxe.h"

/**
  Initialize the state information for the CPU Architectural Protocol

  @param  ImageHandle   of the loaded driver
  @param  SystemTable   Pointer to the System Table

  @retval EFI_SUCCESS           Protocol registered
  @retval EFI_OUT_OF_RESOURCES  Cannot allocate protocol data structure
  @retval EFI_DEVICE_ERROR      Hardware problems
  @retval EFI_UNSUPPORTED       AIC version not supported

**/
EFI_STATUS
InterruptDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  //
  // Unlike the GIC, the AIC has no reliable architectural way to probe the
  // controller version; Linux selects it through the devicetree compatible
  // string. PcdAicVersion serves the same purpose here.
  //
  switch (PcdGet32 (PcdAicVersion)) {
    case 1:
      Status = AicV1DxeInitialize (ImageHandle, SystemTable);
      break;
    case 2:
    case 3:
      Status = AicV2DxeInitialize (ImageHandle, SystemTable);
      break;
    default:
      DEBUG ((
        DEBUG_ERROR,
        "%a: Unsupported AIC version %u\n",
        __func__,
        PcdGet32 (PcdAicVersion)
        ));
      Status = EFI_UNSUPPORTED;
      break;
  }

  return Status;
}
