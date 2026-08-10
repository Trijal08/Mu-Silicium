//
// Copyright (c) 2012-2013, ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

#include <AsmMacroLib.h>

ASM_FUNC (PlatformAssemblyInitialize)
  // pongoOS / checkm8 A-series hand off at EL3 with MMU+caches on.
  // Silicium SEC only tolerates EL1/EL2 (EL1_OR_EL2 spins at EL3), so
  // normalize the EL here, before _ModuleEntryPoint's first EL check.
  mrs   x0, CurrentEL
  cmp   x0, #0xC              // EL3?
  b.eq  3f
  ret                         // EL1: nothing to do

// EL3 -> EL1 (matches uniLoader arch_el3_to_el1)
3:
  msr   sctlr_el1, xzr        // EL1 starts MMU/caches off, known state
  mrs   x0, scr_el3
  orr   x0, x0, #(1 << 10)    // RW: EL1 is AArch64
  orr   x0, x0, #(1 << 0)     // NS: EL1 non-secure
  msr   scr_el3, x0
  mov   x0, #0x3C5            // SPSR: DAIF masked | EL1h
  msr   spsr_el3, x0
  mov   x9, sp
  msr   sp_el1, x9            // carry the stack across the eret
  msr   elr_el3, x30          // return into _ModuleEntryPoint at EL1
  eret
