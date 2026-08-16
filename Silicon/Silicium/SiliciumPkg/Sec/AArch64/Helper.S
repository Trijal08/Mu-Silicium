//
// Copyright (c) 2011-2017, ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

#include <AsmMacroLib.h>

#include <AArch64/AArch64.h>

//
// HCR_EL2 Bits
//
.set HCR_FIQ_BIT,    (1 << 3)
.set HCR_IRQ_BIT,    (1 << 4)
.set HCR_SERROR_BIT, (1 << 5)

ASM_FUNC (SetupExceptionLevel1)
  // Re-Configure Access Control
  mov   x0, #CPACR_DEFAULT
  b     ASM_PFX (ArmWriteCpacr)

ASM_FUNC (SetupExceptionLevel2)
  // Clear SCTRL_EL2
  msr   sctlr_el2, xzr

  // Get current Hypervisor Configuration
  mrs   x0, hcr_el2

  // Redirect Interrupts to EL2
  orr   x0, x0, #HCR_FIQ_BIT
  orr   x0, x0, #HCR_IRQ_BIT
  orr   x0, x0, #HCR_SERROR_BIT

  // Write new Hypervisor Configuration
  msr   hcr_el2, x0

  //
  // Make the Virtual Counter agree with the Physical one.
  //
  // CNTVCT_EL0 reads as CNTPCT_EL0 minus CNTVOFF_EL2, and the Offset is left at
  // whatever the Boot Loader that ran before this put there. Firmware owning EL2 is
  // what decides that Value, and the only sensible Answer when there is no Guest is
  // Zero. This Platform selects the Physical Counter for its own Timing, so nothing
  // here would ever notice a skewed Virtual Counter, but an OS Loader that reads
  // CNTVCT_EL0 to work out how long to wait for something would compute Deadlines that
  // never arrive. Zero it before anything can read it.
  //
  msr   cntvoff_el2, xzr

  // Setup EL1 Exceptions
  tst   x0, #ARM_HCR_E2H
  b.ne  SetupExceptionLevel1

  // Enable Architectural Features
  mov   x0, #AARCH64_CPTR_DEFAULT

  //
  // CPTR_EL2 Bit 8 is only RES1 on Cores without SVE, which is what the shared
  // AARCH64_CPTR_RES1 Value assumes. On an SVE capable Core that Bit is TZ, and
  // leaving it set traps every SVE Access to EL2 - including the Feature Probes a
  // modern OS Loader performs - so clear it when ID_AA64PFR0_EL1.SVE is non-zero.
  //
  mrs   x1, id_aa64pfr0_el1
  ubfx  x1, x1, #32, #4
  cbz   x1, 1f
  bic   x0, x0, #(1 << 8)
1:
  msr   cptr_el2, x0

  ret

ASM_FUNCTION_REMOVE_IF_UNREFERENCED
