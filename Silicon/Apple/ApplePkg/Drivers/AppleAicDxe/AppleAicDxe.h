/*++

Copyright The Asahi Linux Contributors
Copyright (c) 2026, Trijal Saha. All rights reserved.<BR>

Based on the Linux irq-apple-aic driver:
  Copyright The Asahi Linux Contributors

SPDX-License-Identifier: GPL-2.0-or-later

--*/

#ifndef APPLE_AIC_DXE_H_
#define APPLE_AIC_DXE_H_

#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/Cpu.h>
#include <Protocol/HardwareInterrupt.h>
#include <Protocol/HardwareInterrupt2.h>

/*
 * AIC is a fairly simple interrupt controller with the following features:
 *
 * - 896 level-triggered hardware IRQs
 *   - Single mask bit per IRQ
 *   - Per-IRQ affinity setting
 *   - Automatic masking on event delivery (auto-ack)
 *   - Software triggering (ORed with hw line)
 * - Automatic prioritization (single event/ack register per CPU, lower IRQs =
 *   higher priority)
 * - Automatic masking on ack
 * - Default "this CPU" register view and explicit per-CPU views
 *
 * In addition, this driver also handles FIQs, as these are routed to the same
 * IRQ vector. These are used for the ARMv8 timers.
 *
 * Implementation notes:
 *
 * - UEFI only ever runs on a single CPU, so IPIs and the per-IRQ affinity
 *   setting are not used; every IRQ is routed to the boot CPU.
 * - FIQ hwirq numbers are assigned after true hwirqs, and are per-cpu.
 * - Interrupt sources use the same encoding as the AIC event register:
 *   - IRQ #n on die #d is source AIC_IRQ_HWIRQ (d, n)
 *   - FIQ #n is source AIC_FIQ_HWIRQ (n), see AIC_FIQ_INDEX
 */

#ifndef ARM_ARCH_EXCEPTION_FIQ
#define ARM_ARCH_EXCEPTION_FIQ  EXCEPT_AARCH64_FIQ
#endif

/*
 * AIC v1 registers (MMIO)
 */

#define AIC_INFO                0x0004
#define AIC_INFO_NR_IRQ(x)      ((x) & 0xFFFF)

#define AIC_CONFIG              0x0010

#define AIC_WHOAMI              0x2000
#define AIC_EVENT               0x2004

#define AIC_EVENT_TYPE_FIQ      0 /* Software use */
#define AIC_EVENT_TYPE_IRQ      1
#define AIC_EVENT_TYPE_IPI      4

#define AIC_TARGET_CPU          0x3000

#define AIC_MAX_IRQ             0x400

/*
 * AIC v2 registers (MMIO)
 */

#define AIC2_VERSION            0x0000
#define AIC2_VERSION_VER(x)     ((x) & 0xFF)

#define AIC2_INFO1              0x0004
#define AIC2_INFO1_NR_IRQ(x)    ((x) & 0xFFFF)
#define AIC2_INFO1_LAST_DIE(x)  (((x) >> 24) & 0xF)

#define AIC2_INFO2              0x0008

#define AIC2_INFO3              0x000C
#define AIC2_INFO3_MAX_IRQ(x)   ((x) & 0xFFFF)
#define AIC2_INFO3_MAX_DIE(x)   (((x) >> 24) & 0xF)

#define AIC2_RESET              0x0010
#define AIC2_RESET_RESET        BIT0

#define AIC2_CONFIG             0x0014
#define AIC2_CONFIG_ENABLE      BIT0
#define AIC2_CONFIG_PREFER_PCPU BIT28

#define AIC2_TIMEOUT            0x0028
#define AIC2_CLUSTER_PRIO       0x0030
#define AIC2_DELAY_GROUPS       0x0100

#define AIC2_IRQ_CFG            0x2000

/* AIC v3 registers (MMIO) */
#define AIC3_IRQ_CFG            0x10000

/*
 * AIC2 registers are laid out like this, starting at AIC2_IRQ_CFG:
 * AIC3 registers use the same layout but start at AIC3_IRQ_CFG:
 *
 * Repeat for each die:
 *   IRQ_CFG: u32 * MAX_IRQS
 *   SW_SET: u32 * (MAX_IRQS / 32)
 *   SW_CLR: u32 * (MAX_IRQS / 32)
 *   MASK_SET: u32 * (MAX_IRQS / 32)
 *   MASK_CLR: u32 * (MAX_IRQS / 32)
 *   HW_STATE: u32 * (MAX_IRQS / 32)
 *
 * This is followed by a set of event registers, each 16K page aligned.
 * The first one is the AP event register we will use. Unfortunately,
 * the actual implemented die count is not specified anywhere in the
 * capability registers, so the event register base has to be specified
 * explicitly through PcdAicEventBase.
 */

#define AIC_MASK_REG(x)         (4 * ((x) >> 5))
#define AIC_MASK_BIT(x)         (1U << ((x) & 0x1F))

/*
 * Interrupt source encoding, matching the AIC event register layout:
 * Die[31:24], Type[23:16], Num[15:0]
 */

#define AIC_IRQ_HWIRQ(Die, Irq) (((UINTN)(Die) << 24) |                        \
                                 ((UINTN)AIC_EVENT_TYPE_IRQ << 16) |           \
                                 (UINTN)(Irq))
#define AIC_FIQ_HWIRQ(Fiq)      (((UINTN)AIC_EVENT_TYPE_FIQ << 16) |           \
                                 (UINTN)(Fiq))
#define AIC_HWIRQ_IRQ(x)        ((UINT32)(x) & 0xFFFF)
#define AIC_HWIRQ_TYPE(x)       (((UINT32)(x) >> 16) & 0xFF)
#define AIC_HWIRQ_DIE(x)        (((UINT32)(x) >> 24) & 0xFF)

// A timer FIQ is asserted when the timer is enabled, unmasked, and its
// status bit is set.
#define AIC_TIMER_FIRING(Ctl)   (((Ctl) & (ARM_ARCH_TIMER_ENABLE |             \
                                           ARM_ARCH_TIMER_IMASK |              \
                                           ARM_ARCH_TIMER_ISTATUS)) ==         \
                                 (ARM_ARCH_TIMER_ENABLE | ARM_ARCH_TIMER_ISTATUS))

/*
 * FIQ hwirq index definitions: the two timer types are represented by their
 * access method: _EL0 registers or _EL02 registers. The guest timers and the
 * PMCs exist as event numbers but are not supported by this driver.
 */
typedef enum {
  AicTmrEl0Phys = 0,
  AicTmrEl0Virt,
  AicTmrEl02Phys,
  AicTmrEl02Virt,
  AicCpuPmuE,
  AicCpuPmuP,
  AicNrFiq
} AIC_FIQ_INDEX;

typedef struct {
  UINT32    Version;

  // Register offsets
  UINT32    Event;
  UINT32    TargetCpu;
  UINT32    IrqCfg;
  UINT32    SwSet;
  UINT32    SwClr;
  UINT32    MaskSet;
  UINT32    MaskClr;

  UINT32    DieStride;
} AIC_CHIP_INFO;

typedef struct {
  UINTN            Base;
  UINTN            EventReg;

  UINT32           NrIrq;
  UINT32           MaxIrq;
  UINT32           NrDie;
  UINT32           MaxDie;

  AIC_CHIP_INFO    Info;
} AIC_IRQ_CHIP;

extern EFI_CPU_ARCH_PROTOCOL  *gCpuArch;
extern AIC_IRQ_CHIP           gAic;

// AicV1 API
EFI_STATUS
AicV1DxeInitialize (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  );

// AicV2/AicV3 API
EFI_STATUS
AicV2DxeInitialize (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  );

// Shared code

UINT32
AicIcRead (
  IN AIC_IRQ_CHIP  *Ic,
  IN UINT32        Reg
  );

VOID
AicIcWrite (
  IN AIC_IRQ_CHIP  *Ic,
  IN UINT32        Reg,
  IN UINT32        Value
  );

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
  );

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
  );

EFI_STATUS
EFIAPI
AicCommonRegisterInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source,
  IN HARDWARE_INTERRUPT_HANDLER       Handler,
  IN HARDWARE_INTERRUPT_HANDLER       *HandlerDest
  );

EFI_STATUS
AicCommonInstallAndRegisterInterruptService (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL   *InterruptProtocol,
  IN EFI_HARDWARE_INTERRUPT2_PROTOCOL  *Interrupt2Protocol,
  IN EFI_CPU_INTERRUPT_HANDLER         IrqInterruptHandler,
  IN EFI_CPU_INTERRUPT_HANDLER         FiqInterruptHandler,
  IN EFI_EVENT_NOTIFY                  ExitBootServicesEvent
  );

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
  );

#endif // APPLE_AIC_DXE_H_
