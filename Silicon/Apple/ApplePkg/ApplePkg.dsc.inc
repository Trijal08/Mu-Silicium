##
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2026, Trijal Saha. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

!include SiliciumPkg/SiliciumPkg.dsc.inc

[BuildOptions.common.EDKII.DXE_DRIVER, BuildOptions.common.EDKII.UEFI_DRIVER, BuildOptions.common.EDKII.UEFI_APPLICATION]
  #
  # The Apple A-series MMU uses a 16 KB translation granule. Link DXE drivers
  # and applications with 16 KB section alignment so the DxeCore memory-
  # protection policy can apply per-section RO/NX at the granule the MMU uses
  # (paired with "Align = Auto" in SiliciumPkg/Common.fdf.inc, which propagates
  # this into the FV). Overrides the SiliciumPkg default of /ALIGN:0x1000.
  #
  # DXE_CORE is deliberately left at /ALIGN:0x1000: it is loaded by PrePi
  # (LoadAndRelocatePeCoffImage -> AllocateCodePages), which only produces a
  # 4 KB-aligned buffer and would reject a 16 KB-aligned image. Its residual
  # sub-granule attribute requests are absorbed by the skip in the local
  # CpuDxe's CpuSetMemoryAttributes. DXE_RUNTIME_DRIVER already links
  # /ALIGN:0x10000 (a multiple of 16 KB) and needs no change.
  #
  *_CLANGPDB_*_DLINK_FLAGS = /ALIGN:0x4000

[LibraryClasses]
  #
  # MMU
  #
  # Apple A-series cores (e.g. A9/S8003) do not implement the 4 KB translation
  # granule and require 16 KB. Override the stock Mu_Basecore ArmMmuLib with a
  # local copy that selects the granule at runtime from ID_AA64MMFR0_EL1, so
  # Mu_Basecore itself stays unmodified.
  #
  ArmMmuLib|ApplePkg/Library/ArmMmuLib/ArmMmuBaseLib.inf

  #
  # Graphics Console
  #
  # Local copy of PcBdsPkg's GraphicsConsoleHelperLib without the 800px native-
  # resolution floor. n69's 640-wide panel is below that floor, so the stock
  # library resolves GCM_NATIVE_RES to a non-existent 800x600 mode and
  # SetGraphicsConsoleMode() fails; MsBootPolicy gates the boot logo redraw on
  # that success, so the logo never reappears after the boot menu.
  #
  GraphicsConsoleHelperLib|ApplePkg/Library/GraphicsConsoleHelperLib/GraphicsConsoleHelper.inf

  #
  # Reset / Power
  #
  # Apple A-series booted via pongoOS has no PSCI secure monitor, so the stock
  # ArmPsciResetSystemLib (SMC SYSTEM_RESET/SYSTEM_OFF) is a no-op. Reboot goes
  # through the SoC watchdog timer instead (same as pongoOS).
  #
  ResetSystemLib|ApplePkg/Library/ResetSystemLib/ResetSystemLib.inf
  HwResetSystemLib|ApplePkg/Library/ResetSystemLib/ResetSystemLib.inf

[PcdsFixedAtBuild]
  #
  # Timer
  #
  # The ARMv8 timers are routed through the AIC as FIQ sources:
  # 0 = EL0 physical timer, 1 = EL0 virtual timer.
  # There is no hypervisor timer interrupt (0 skips its registration) and no
  # secure timer either; its slot is parked on a valid FIQ index that
  # AppleAicDxe never dispatches.
  #
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|0
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|1
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|0
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|2

[Components]
  #
  # Processor Core Services
  #
  # Local CpuDxe whose page-table walk is granule-aware (16 KB on Apple
  # A-series); replaces ArmPkg/Drivers/CpuDxe/CpuDxe.inf in the FDF.
  #
  ApplePkg/Drivers/CpuDxe/CpuDxe.inf

  #
  # Interrupt Controller Drivers
  #
  ApplePkg/Drivers/AppleAicDxe/AppleAicDxe.inf

  #
  # IO Drivers
  #
  ApplePkg/Drivers/GpioDxe/GpioDxe.inf
