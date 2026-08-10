/*++

Copyright The Asahi Linux Contributors
Copyright (c) 2026, Trijal Saha. All rights reserved.<BR>

Based on the Linux irq-apple-aic driver:
  Copyright The Asahi Linux Contributors

SPDX-License-Identifier: GPL-2.0-or-later

--*/

#include "AppleAicDxe.h"
#include "Uefi/UefiBaseType.h"

// Making this global saves a few bytes in image size
EFI_HANDLE  gHardwareInterruptHandle = NULL;

// Notifications
EFI_EVENT  EfiExitBootServicesEvent = (EFI_EVENT)NULL;

EFI_CPU_ARCH_PROTOCOL  *gCpuArch;

// The AIC instance driven by this driver
AIC_IRQ_CHIP  gAic;

// Registered interrupt handlers, one per IRQ on every die
STATIC HARDWARE_INTERRUPT_HANDLER  *mIrqHandlers;

// Registered interrupt handlers, one per FIQ source
STATIC HARDWARE_INTERRUPT_HANDLER  mFiqHandlers[AicNrFiq];

// Software copy of the hardware IRQ mask state; reading the event register
// automatically masks the acknowledged IRQ, so the hardware state cannot be
// used to tell an acknowledged IRQ from a disabled one.
STATIC UINT32  *mIrqEnabled;

// Software mask state of the FIQ sources, one bit per AIC_FIQ_INDEX
STATIC UINT32  mFiqUnmasked;

UINT32
AicIcRead (
  IN AIC_IRQ_CHIP  *Ic,
  IN UINT32        Reg
  )
{
  return MmioRead32 (Ic->Base + Reg);
}

VOID
AicIcWrite (
  IN AIC_IRQ_CHIP  *Ic,
  IN UINT32        Reg,
  IN UINT32        Value
  )
{
  MmioWrite32 (Ic->Base + Reg, Value);
}

/**
 *
 * Return whether the Source interrupt index refers to a hardware IRQ.
 *
 * @param Source  The source intid to test
 *
 * @return True if Source is a hardware IRQ intid, false otherwise.
 */
BOOLEAN
AicCommonSourceIsIrq (
  IN UINTN  Source
  )
{
  return AIC_HWIRQ_TYPE (Source) == AIC_EVENT_TYPE_IRQ;
}

/**
 *
 * Return whether the Source interrupt index refers to a FIQ.
 *
 * @param Source  The source intid to test
 *
 * @return True if Source is a FIQ intid, false otherwise.
 */
BOOLEAN
AicCommonSourceIsFiq (
  IN UINTN  Source
  )
{
  return AIC_HWIRQ_TYPE (Source) == AIC_EVENT_TYPE_FIQ;
}

/**
 * Return whether this is a valid interrupt source for this AIC.
 *
 * @param Source  The source intid to test
 *
 * @return True if Source is implemented on this AIC, false otherwise.
 */
STATIC
BOOLEAN
AicIsValidSource (
  IN HARDWARE_INTERRUPT_SOURCE  Source
  )
{
  if (AicCommonSourceIsIrq (Source)) {
    return (AIC_HWIRQ_DIE (Source) < gAic.NrDie) &&
           (AIC_HWIRQ_IRQ (Source) < gAic.NrIrq);
  }

  if (AicCommonSourceIsFiq (Source)) {
    return (AIC_HWIRQ_DIE (Source) == 0) &&
           (AIC_HWIRQ_IRQ (Source) < AicNrFiq);
  }

  return FALSE;
}

/*
 * IRQ irqchip
 */

/**
 * Gets the address of the software IRQ enable state word for Source.
 *
 * @param Source  Hardware source of the interrupt
 *
 * @return Pointer to the state word holding the enable bit for Source.
 */
STATIC
UINT32 *
AicIrqEnabledWord (
  IN HARDWARE_INTERRUPT_SOURCE  Source
  )
{
  UINT32  WordsPerDie;

  WordsPerDie = (gAic.NrIrq + 31) / 32;

  return &mIrqEnabled[(AIC_HWIRQ_DIE (Source) * WordsPerDie)
                      + (AIC_HWIRQ_IRQ (Source) >> 5)];
}

STATIC
VOID
AicIrqMask (
  IN HARDWARE_INTERRUPT_SOURCE  Source
  )
{
  UINT32  Off;
  UINT32  Irq;

  Off = AIC_HWIRQ_DIE (Source) * gAic.Info.DieStride;
  Irq = AIC_HWIRQ_IRQ (Source);

  AicIcWrite (&gAic, gAic.Info.MaskSet + Off + AIC_MASK_REG (Irq), AIC_MASK_BIT (Irq));
}

STATIC
VOID
AicIrqUnmask (
  IN HARDWARE_INTERRUPT_SOURCE  Source
  )
{
  UINT32  Off;
  UINT32  Irq;

  Off = AIC_HWIRQ_DIE (Source) * gAic.Info.DieStride;
  Irq = AIC_HWIRQ_IRQ (Source);

  AicIcWrite (&gAic, gAic.Info.MaskClr + Off + AIC_MASK_REG (Irq), AIC_MASK_BIT (Irq));
}

/*
 * FIQ irqchip
 */

/**
  Mask a FIQ source.

  Only the guest timers have real mask bits in the AIC itself. The EL0 timers
  are masked through their architectural IMASK bit instead, which this driver
  owns in UEFI. The guest timers and the PMCs are not supported.

  @param FiqIndex  AIC_FIQ_INDEX of the FIQ source
**/
STATIC
VOID
AicFiqSetMask (
  IN UINTN  FiqIndex
  )
{
  switch (FiqIndex) {
    case AicTmrEl0Phys:
      ArmWriteCntpCtl (ArmReadCntpCtl () | ARM_ARCH_TIMER_IMASK);
      break;
    case AicTmrEl0Virt:
      ArmWriteCntvCtl (ArmReadCntvCtl () | ARM_ARCH_TIMER_IMASK);
      break;
    default:
      break;
  }
}

STATIC
VOID
AicFiqClearMask (
  IN UINTN  FiqIndex
  )
{
  switch (FiqIndex) {
    case AicTmrEl0Phys:
      ArmWriteCntpCtl (ArmReadCntpCtl () & ~(UINTN)ARM_ARCH_TIMER_IMASK);
      break;
    case AicTmrEl0Virt:
      ArmWriteCntvCtl (ArmReadCntvCtl () & ~(UINTN)ARM_ARCH_TIMER_IMASK);
      break;
    default:
      break;
  }
}

/**
  Gets the address of the interrupt callback associated with Source.

  @param  Source    Hardware source of the interrupt

  @return Pointer to the associated handler
          NULL if the source is invalid.
**/
STATIC
HARDWARE_INTERRUPT_HANDLER *
AicGetHandlerAddress (
  IN HARDWARE_INTERRUPT_SOURCE  Source
  )
{
  if (!AicIsValidSource (Source)) {
    return NULL;
  }

  if (AicCommonSourceIsFiq (Source)) {
    return &mFiqHandlers[AIC_HWIRQ_IRQ (Source)];
  }

  return &mIrqHandlers[(AIC_HWIRQ_DIE (Source) * gAic.NrIrq)
                       + AIC_HWIRQ_IRQ (Source)];
}

/**
  Enable interrupt source Source.

  @param This     Instance pointer for this protocol
  @param Source   Hardware source of the interrupt

  @retval EFI_SUCCESS       Source interrupt enabled.
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.

**/
STATIC
EFI_STATUS
EFIAPI
AicEnableInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source
  )
{
  if (!AicIsValidSource (Source)) {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  if (AicCommonSourceIsIrq (Source)) {
    *AicIrqEnabledWord (Source) |= AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source));
    AicIrqUnmask (Source);
  } else {
    mFiqUnmasked |= AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source));
    AicFiqClearMask (AIC_HWIRQ_IRQ (Source));
  }

  return EFI_SUCCESS;
}

/**
  Disable interrupt source Source.

  @param This     Instance pointer for this protocol
  @param Source   Hardware source of the interrupt

  @retval EFI_SUCCESS       Source interrupt disabled.
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.

**/
STATIC
EFI_STATUS
EFIAPI
AicDisableInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source
  )
{
  if (!AicIsValidSource (Source)) {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  if (AicCommonSourceIsIrq (Source)) {
    AicIrqMask (Source);
    *AicIrqEnabledWord (Source) &= ~AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source));
  } else {
    AicFiqSetMask (AIC_HWIRQ_IRQ (Source));
    mFiqUnmasked &= ~AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source));
  }

  return EFI_SUCCESS;
}

/**
  Return current state of interrupt source Source.

  @param This     Instance pointer for this protocol
  @param Source   Hardware source of the interrupt
  @param InterruptState  TRUE: source enabled, FALSE: source disabled.

  @retval EFI_SUCCESS       InterruptState is valid
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.

**/
STATIC
EFI_STATUS
EFIAPI
AicGetInterruptSourceState (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source,
  IN BOOLEAN                          *InterruptState
  )
{
  if (!AicIsValidSource (Source)) {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  if (AicCommonSourceIsIrq (Source)) {
    *InterruptState = (*AicIrqEnabledWord (Source)
                       & AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source))) != 0;
  } else {
    *InterruptState = (mFiqUnmasked
                       & AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source))) != 0;
  }

  return EFI_SUCCESS;
}

/**
  Signal to the hardware that the End Of Interrupt state
  has been reached.

  @param This     Instance pointer for this protocol
  @param Source   Hardware source of the interrupt

  @retval EFI_SUCCESS       Source interrupt ended successfully.
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.

**/
STATIC
EFI_STATUS
EFIAPI
AicEndOfInterrupt (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source
  )
{
  if (!AicIsValidSource (Source)) {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  //
  // Reading the interrupt reason automatically acknowledges and masks
  // the IRQ, so we just unmask it here if needed.
  //
  if (AicCommonSourceIsIrq (Source)) {
    if ((*AicIrqEnabledWord (Source) & AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source))) != 0) {
      AicIrqUnmask (Source);
    }
  } else {
    if ((mFiqUnmasked & AIC_MASK_BIT (AIC_HWIRQ_IRQ (Source))) != 0) {
      AicFiqClearMask (AIC_HWIRQ_IRQ (Source));
    }
  }

  return EFI_SUCCESS;
}

/**
  Register Handler for the specified interrupt source.

  @param This        Instance pointer for this protocol
  @param Source      Hardware source of the interrupt
  @param Handler     Callback for interrupt. NULL to unregister
  @param HandlerDest Address of where to store Handler.

  @retval EFI_SUCCESS Source was updated to support Handler.
  @retval EFI_DEVICE_ERROR  Hardware could not be programmed.

**/
EFI_STATUS
EFIAPI
AicCommonRegisterInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source,
  IN HARDWARE_INTERRUPT_HANDLER       Handler,
  IN HARDWARE_INTERRUPT_HANDLER       *HandlerDest
  )
{
  EFI_STATUS  Status;

  if (HandlerDest == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Handler == NULL) && (*HandlerDest == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Handler != NULL) && (*HandlerDest != NULL)) {
    return EFI_ALREADY_STARTED;
  }

  if (NULL == Handler) {
    // Removing the handler - disable the interrupt first.
    Status       = This->DisableInterruptSource (This, Source);
    *HandlerDest = Handler;
    return Status;
  } else {
    // Registering a handler - set up the handler before enabling.
    *HandlerDest = Handler;
    return This->EnableInterruptSource (This, Source);
  }
}

EFI_STATUS
EFIAPI
AicRegisterInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source,
  IN HARDWARE_INTERRUPT_HANDLER       Handler
  )
{
  HARDWARE_INTERRUPT_HANDLER  *HandlerDest = AicGetHandlerAddress (Source);

  if (HandlerDest == NULL) {
    return EFI_UNSUPPORTED;
  }

  return AicCommonRegisterInterruptSource (
           This,
           Source,
           Handler,
           HandlerDest
           );
}

// The protocol instance produced by this driver
EFI_HARDWARE_INTERRUPT_PROTOCOL  gHardwareInterruptAicProtocol = {
  AicRegisterInterruptSource,
  AicEnableInterruptSource,
  AicDisableInterruptSource,
  AicGetInterruptSourceState,
  AicEndOfInterrupt
};

/**
  Get interrupt trigger type of an interrupt

  @param This          Instance pointer for this protocol
  @param Source        Hardware source of the interrupt.
  @param TriggerType   Returns interrupt trigger type.

  @retval EFI_SUCCESS       Source interrupt supported.
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.

**/
STATIC
EFI_STATUS
EFIAPI
AicGetTriggerType (
  IN  EFI_HARDWARE_INTERRUPT2_PROTOCOL      *This,
  IN  HARDWARE_INTERRUPT_SOURCE             Source,
  OUT EFI_HARDWARE_INTERRUPT2_TRIGGER_TYPE  *TriggerType
  )
{
  if (!AicIsValidSource (Source)) {
    return EFI_UNSUPPORTED;
  }

  //
  // AIC interrupts are level triggered and there is no way to find out
  // the type of any given IRQ.
  //
  *TriggerType = EFI_HARDWARE_INTERRUPT2_TRIGGER_LEVEL_HIGH;

  return EFI_SUCCESS;
}

/**
  Set interrupt trigger type of an interrupt

  @param This          Instance pointer for this protocol
  @param Source        Hardware source of the interrupt.
  @param TriggerType   Interrupt trigger type.

  @retval EFI_SUCCESS       Source interrupt supported.
  @retval EFI_UNSUPPORTED   Source interrupt is not supported.
**/
STATIC
EFI_STATUS
EFIAPI
AicSetTriggerType (
  IN  EFI_HARDWARE_INTERRUPT2_PROTOCOL      *This,
  IN  HARDWARE_INTERRUPT_SOURCE             Source,
  IN  EFI_HARDWARE_INTERRUPT2_TRIGGER_TYPE  TriggerType
  )
{
  if (!AicIsValidSource (Source)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Some IRQs (e.g. MSIs) implicitly have edge semantics, and there is no
  // way to find out or configure the type of any given IRQ, so just allow
  // both without programming anything.
  //
  if (  (TriggerType != EFI_HARDWARE_INTERRUPT2_TRIGGER_LEVEL_HIGH)
     && (TriggerType != EFI_HARDWARE_INTERRUPT2_TRIGGER_EDGE_RISING))
  {
    DEBUG ((
      DEBUG_ERROR,
      "Invalid interrupt trigger type: %d\n", \
      TriggerType
      ));
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_HARDWARE_INTERRUPT2_PROTOCOL  gHardwareInterrupt2AicProtocol = {
  (HARDWARE_INTERRUPT2_REGISTER)AicRegisterInterruptSource,
  (HARDWARE_INTERRUPT2_ENABLE)AicEnableInterruptSource,
  (HARDWARE_INTERRUPT2_DISABLE)AicDisableInterruptSource,
  (HARDWARE_INTERRUPT2_INTERRUPT_STATE)AicGetInterruptSourceState,
  (HARDWARE_INTERRUPT2_END_OF_INTERRUPT)AicEndOfInterrupt,
  AicGetTriggerType,
  AicSetTriggerType
};

/**
  EFI_CPU_INTERRUPT_HANDLER that is called when a processor IRQ occurs.

  @param  InterruptType    Defines the type of interrupt or exception that
                           occurred on the processor. This parameter is
                           processor architecture specific.
  @param  SystemContext    A pointer to the processor context when
                           the interrupt occurred on the processor.

  @return None

**/
STATIC
VOID
EFIAPI
AicIrqInterruptHandler (
  IN EFI_EXCEPTION_TYPE  InterruptType,
  IN EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  UINT32                      Event;
  UINT32                      Type;
  HARDWARE_INTERRUPT_HANDLER  *HandlerPtr;

  do {
    Event = MmioRead32 (gAic.EventReg + gAic.Info.Event);
    Type  = AIC_HWIRQ_TYPE (Event);

    if (Type == AIC_EVENT_TYPE_IRQ) {
      HandlerPtr = AicGetHandlerAddress (Event);
      if ((HandlerPtr != NULL) && (*HandlerPtr != NULL)) {
        // Call the registered interrupt handler.
        (*HandlerPtr)(Event, SystemContext);
      } else {
        //
        // Reading the event register has already acknowledged and masked
        // this IRQ, so it is safe to just leave it masked.
        //
        DEBUG ((DEBUG_ERROR, "Spurious AIC interrupt: 0x%x\n", Event));
      }
    } else if (Event != 0) {
      DEBUG ((
        DEBUG_ERROR,
        "Unknown AIC event %u, %u\n",
        Type,
        AIC_HWIRQ_IRQ (Event)
        ));
    }
  } while (Event != 0);
}

/**
  Dispatch one asserted FIQ source to its registered handler.

  @param  FiqIndex         AIC_FIQ_INDEX of the FIQ source
  @param  SystemContext    A pointer to the processor context when
                           the interrupt occurred on the processor.

  @return None

**/
STATIC
VOID
AicFiqDispatch (
  IN UINTN               FiqIndex,
  IN EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  HARDWARE_INTERRUPT_HANDLER  Handler;

  Handler = mFiqHandlers[FiqIndex];

  if (Handler != NULL) {
    // Call the registered interrupt handler.
    Handler (AIC_FIQ_HWIRQ (FiqIndex), SystemContext);
  } else {
    //
    // Not dealing with an asserted FIQ source results in a FIQ storm,
    // so mask anything that fires without a registered handler.
    //
    DEBUG ((DEBUG_ERROR, "Spurious AIC FIQ %u fired. Masking.\n", (UINT32)FiqIndex));
    AicFiqSetMask (FiqIndex);
  }
}

/**
  EFI_CPU_INTERRUPT_HANDLER that is called when a processor FIQ occurs.

  There is no register that reports the FIQ source state, so every potential
  source has to be peeked at individually. Only the ARMv8 timers are checked
  here; the other FIQ sources (Fast IPIs, PMCs, uncore PMCs) are never
  enabled by this driver.

  @param  InterruptType    Defines the type of interrupt or exception that
                           occurred on the processor. This parameter is
                           processor architecture specific.
  @param  SystemContext    A pointer to the processor context when
                           the interrupt occurred on the processor.

  @return None

**/
STATIC
VOID
EFIAPI
AicFiqInterruptHandler (
  IN EFI_EXCEPTION_TYPE  InterruptType,
  IN EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  if (AIC_TIMER_FIRING (ArmReadCntpCtl ())) {
    AicFiqDispatch (AicTmrEl0Phys, SystemContext);
  }

  if (AIC_TIMER_FIRING (ArmReadCntvCtl ())) {
    AicFiqDispatch (AicTmrEl0Virt, SystemContext);
  }
}

/**
  Shutdown our hardware

  DXE Core will disable interrupts and turn off the timer and disable
  interrupts after all the event handlers have run.

  @param[in]  Event   The Event that is being processed
  @param[in]  Context Event Context
**/
STATIC
VOID
EFIAPI
AicExitBootServicesEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT32  Die;
  UINT32  Index;
  UINT32  Off;
  UINT32  WordsPerDie;

  // Mask all hardware IRQs on every die
  WordsPerDie = (gAic.NrIrq + 31) / 32;
  Off         = 0;
  for (Die = 0; Die < gAic.NrDie; Die++) {
    for (Index = 0; Index < WordsPerDie; Index++) {
      AicIcWrite (&gAic, gAic.Info.MaskSet + Off + (Index * 4), MAX_UINT32);
    }

    Off += gAic.Info.DieStride;
  }

  // Mask the timer FIQs
  AicFiqSetMask (AicTmrEl0Phys);
  AicFiqSetMask (AicTmrEl0Virt);
  mFiqUnmasked = 0;
}

EFI_STATUS
AicCommonInstallAndRegisterInterruptService (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL   *InterruptProtocol,
  IN EFI_HARDWARE_INTERRUPT2_PROTOCOL  *Interrupt2Protocol,
  IN EFI_CPU_INTERRUPT_HANDLER         IrqInterruptHandler,
  IN EFI_CPU_INTERRUPT_HANDLER         FiqInterruptHandler,
  IN EFI_EVENT_NOTIFY                  ExitBootServicesEvent
  )
{
  EFI_STATUS  Status;

  // Register to receive IRQ interrupts
  Status = gCpuArch->RegisterInterruptHandler (gCpuArch, ARM_ARCH_EXCEPTION_IRQ, IrqInterruptHandler);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cpu->RegisterInterruptHandler() - %r\n", __func__, Status));
    return Status;
  }

  // The FIQ sources (i.e. the timers) are routed to the FIQ vector
  Status = gCpuArch->RegisterInterruptHandler (gCpuArch, ARM_ARCH_EXCEPTION_FIQ, FiqInterruptHandler);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cpu->RegisterInterruptHandler() - %r\n", __func__, Status));
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gHardwareInterruptHandle,
                  &gHardwareInterruptProtocolGuid,
                  InterruptProtocol,
                  &gHardwareInterrupt2ProtocolGuid,
                  Interrupt2Protocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register for an ExitBootServicesEvent
  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  ExitBootServicesEvent,
                  NULL,
                  &EfiExitBootServicesEvent
                  );

  return Status;
}

/**
  Finish initializing the AIC once the version specific discovery is done.

  Expects gAic.Base, gAic.EventReg, the IRQ/die counts and the version
  specific members of gAic.Info to be filled in by the caller.

  @retval EFI_SUCCESS           Protocol registered
  @retval EFI_OUT_OF_RESOURCES  Cannot allocate protocol data structure
  @retval EFI_DEVICE_ERROR      Hardware problems
**/
EFI_STATUS
AicCommonInitialize (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      StartOff;
  UINT32      Off;
  UINT32      Die;
  UINT32      Index;
  UINT32      WordsPerDie;
  UINTN       RegionSize;

  //
  // The register blocks after the per-IRQ configuration words are laid out
  // identically on all AIC versions, they just start at different offsets.
  //
  StartOff = (gAic.Info.Version == 1) ? gAic.Info.TargetCpu : gAic.Info.IrqCfg;

  Off  = StartOff;
  Off += sizeof (UINT32) * gAic.MaxIrq; /* TARGET_CPU / IRQ_CFG */

  gAic.Info.SwSet = Off;
  Off            += sizeof (UINT32) * (gAic.MaxIrq >> 5); /* SW_SET */
  gAic.Info.SwClr = Off;
  Off            += sizeof (UINT32) * (gAic.MaxIrq >> 5); /* SW_CLR */
  gAic.Info.MaskSet = Off;
  Off              += sizeof (UINT32) * (gAic.MaxIrq >> 5); /* MASK_SET */
  gAic.Info.MaskClr = Off;
  Off              += sizeof (UINT32) * (gAic.MaxIrq >> 5); /* MASK_CLR */
  Off += sizeof (UINT32) * (gAic.MaxIrq >> 5);              /* HW_STATE */

  gAic.Info.DieStride = Off - StartOff;

  // Map the whole register space now that its extent is known.
  RegionSize = ALIGN_VALUE (
                 StartOff + (gAic.Info.DieStride * gAic.NrDie),
                 EFI_PAGE_SIZE
                 );
  Status = gCpuArch->SetMemoryAttributes (
                       gCpuArch,
                       gAic.Base,
                       RegionSize,
                       EFI_MEMORY_UC | EFI_MEMORY_XP
                       );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to map AIC MMIO interface: %r\n", __func__, Status));
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  //
  // Mask all hardware IRQs, clear all pending software IRQs and, where the
  // hardware supports it, route everything to the boot CPU.
  //
  WordsPerDie = (gAic.NrIrq + 31) / 32;
  Off         = 0;
  for (Die = 0; Die < gAic.NrDie; Die++) {
    for (Index = 0; Index < WordsPerDie; Index++) {
      AicIcWrite (&gAic, gAic.Info.MaskSet + Off + (Index * 4), MAX_UINT32);
    }

    for (Index = 0; Index < WordsPerDie; Index++) {
      AicIcWrite (&gAic, gAic.Info.SwClr + Off + (Index * 4), MAX_UINT32);
    }

    if (gAic.Info.TargetCpu != 0) {
      for (Index = 0; Index < gAic.NrIrq; Index++) {
        AicIcWrite (&gAic, gAic.Info.TargetCpu + Off + (Index * 4), 1);
      }
    }

    Off += gAic.Info.DieStride;
  }

  // Mask the timer FIQs until handlers are registered for them
  AicFiqSetMask (AicTmrEl0Phys);
  AicFiqSetMask (AicTmrEl0Virt);
  mFiqUnmasked = 0;

  mIrqHandlers = AllocateZeroPool (
                   sizeof (HARDWARE_INTERRUPT_HANDLER) *
                   gAic.NrDie * gAic.NrIrq
                   );
  if (mIrqHandlers == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mIrqEnabled = AllocateZeroPool (
                  sizeof (UINT32) * gAic.NrDie * WordsPerDie
                  );
  if (mIrqEnabled == NULL) {
    FreePool (mIrqHandlers);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = AicCommonInstallAndRegisterInterruptService (
             &gHardwareInterruptAicProtocol,
             &gHardwareInterrupt2AicProtocol,
             AicIrqInterruptHandler,
             AicFiqInterruptHandler,
             AicExitBootServicesEvent
             );
  if (EFI_ERROR (Status)) {
    FreePool (mIrqHandlers);
    FreePool (mIrqEnabled);
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "AICv%u: Initialized with %u/%u IRQs * %u/%u die(s), %u FIQs\n",
    gAic.Info.Version,
    gAic.NrIrq,
    gAic.MaxIrq,
    gAic.NrDie,
    gAic.MaxDie,
    AicNrFiq
    ));

  return EFI_SUCCESS;
}
