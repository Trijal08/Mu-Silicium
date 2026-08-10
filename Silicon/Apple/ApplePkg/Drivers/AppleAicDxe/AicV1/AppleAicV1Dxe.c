/** @file
*
*  Copyright The Asahi Linux Contributors
*  Copyright (c) 2026, Trijal Saha. All rights reserved.<BR>
*
*  Based on the Linux irq-apple-aic driver:
*    Copyright The Asahi Linux Contributors
*
*  SPDX-License-Identifier: GPL-2.0-or-later
*
**/

#include "AppleAicDxe.h"

/**
  Initialize the state information for the CPU Architectural Protocol

  @param  ImageHandle   of the loaded driver
  @param  SystemTable   Pointer to the System Table

  @retval EFI_SUCCESS           Protocol registered
  @retval EFI_OUT_OF_RESOURCES  Cannot allocate protocol data structure
  @retval EFI_DEVICE_ERROR      Hardware problems

**/
EFI_STATUS
AicV1DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Info;

  // Make sure the Interrupt Controller Protocol is not already installed in
  // the system.
  ASSERT_PROTOCOL_ALREADY_INSTALLED (NULL, &gHardwareInterruptProtocolGuid);

  // Locate the CPU arch protocol - cannot fail because of DEPEX
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&gCpuArch);
  ASSERT_EFI_ERROR (Status);

  gAic.Base = (UINTN)PcdGet64 (PcdAicDistributorBase);

  //
  // Map the capability registers first; the full extent of the register
  // space is only known once they have been read.
  //
  Status = gCpuArch->SetMemoryAttributes (
                       gCpuArch,
                       gAic.Base,
                       SIZE_4KB,
                       EFI_MEMORY_UC | EFI_MEMORY_XP
                       );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to map AICv1 MMIO interface: %r\n", __func__, Status));
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Info = AicIcRead (&gAic, AIC_INFO);

  gAic.NrIrq  = AIC_INFO_NR_IRQ (Info);
  gAic.MaxIrq = AIC_MAX_IRQ;
  gAic.NrDie  = 1;
  gAic.MaxDie = 1;

  gAic.Info.Version   = 1;
  gAic.Info.Event     = AIC_EVENT;
  gAic.Info.TargetCpu = AIC_TARGET_CPU;

  // On AICv1 the event register lives in the main register block
  gAic.EventReg = gAic.Base;

  return AicCommonInitialize ();
}
