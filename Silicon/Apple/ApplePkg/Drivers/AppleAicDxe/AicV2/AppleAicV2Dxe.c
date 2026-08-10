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
AicV2DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT32      Version;
  UINT32      Info1;
  UINT32      Info3;
  UINT32      Config;

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
    DEBUG ((DEBUG_ERROR, "%a: Failed to map AICv2 MMIO interface: %r\n", __func__, Status));
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  //
  // Linux tells AICv2 and AICv3 apart by their devicetree compatible
  // string; the version PCD serves the same purpose here.
  //
  Version = PcdGet32 (PcdAicVersion);

  Info1 = AicIcRead (&gAic, AIC2_INFO1);
  Info3 = AicIcRead (&gAic, AIC2_INFO3);

  gAic.NrIrq  = AIC2_INFO1_NR_IRQ (Info1);
  gAic.MaxIrq = AIC2_INFO3_MAX_IRQ (Info3);
  gAic.NrDie  = AIC2_INFO1_LAST_DIE (Info1) + 1;
  gAic.MaxDie = AIC2_INFO3_MAX_DIE (Info3);

  gAic.Info.Version = Version;
  gAic.Info.IrqCfg  = (Version == 3) ? AIC3_IRQ_CFG : AIC2_IRQ_CFG;

  //
  // The actual implemented die count is not specified anywhere in the
  // capability registers, so the AP event register cannot be derived from
  // the main register block and has to be specified explicitly.
  //
  gAic.EventReg = (UINTN)PcdGet64 (PcdAicEventBase);
  if (gAic.EventReg == 0) {
    DEBUG ((DEBUG_ERROR, "%a: PcdAicEventBase is not set\n", __func__));
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  gAic.Info.Event = 0;

  Status = gCpuArch->SetMemoryAttributes (
                       gCpuArch,
                       gAic.EventReg,
                       SIZE_16KB,
                       EFI_MEMORY_UC | EFI_MEMORY_XP
                       );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to map AICv2 event register: %r\n", __func__, Status));
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = AicCommonInitialize ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Enable the interrupt controller
  Config  = AicIcRead (&gAic, AIC2_CONFIG);
  Config |= AIC2_CONFIG_ENABLE;
  AicIcWrite (&gAic, AIC2_CONFIG, Config);

  return Status;
}
