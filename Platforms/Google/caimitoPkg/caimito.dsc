##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = caimito
  PLATFORM_GUID                  = 9146BB63-EE16-4238-A6C6-36472B925699
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/caimitoPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = caimitoPkg/caimito.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include S5P9875Pkg/S5P9875Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # Debug Output
  #
  # The Default Mask leaves DEBUG_INFO off, which hides the Storage and Link
  # Bring-Up Messages. Remove the 0x40 Bit once UFS is stable.
  #
!if $(TARGET) == DEBUG
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8007EE4F
!endif

  #
  # DDR Memory
  #
  # Base is the first DRAM Bank (2 GB at 0x80000000). It must stay at BASE_2GB:
  # RamManagerLib derives the whole Bank Layout from it (Bank 2 at 0x880000000).
  # Size is the total visible DRAM: 16 GB minus the ABL Carveouts at the Top of
  # DRAM, rounded down for Safety. The Boot Loader does not provide a Samsung
  # SoC Info Structure, so RamManagerLib reads this PCD instead.
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x3E0000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA0212000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Google"
!if $(DEVICE_MODEL) == 0
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 9 Pro XL"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"komodo"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_9_Pro_XL_komodo"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 9 Pro XL"
!elseif $(DEVICE_MODEL) == 1
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 9 Pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"caiman"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_9_Pro_caiman"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 9 Pro"
!elseif $(DEVICE_MODEL) == 2
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 9"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"tokay"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_9_tokay"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 9"
!elseif $(DEVICE_MODEL) == 3
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 9a"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"tegu"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_9a_tegu"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 9a"
!elseif $(DEVICE_MODEL) == 4
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pixel 9 Pro Fold"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"comet"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Pixel_9_Pro_Fold_comet"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Pixel 9 Pro Fold"
!endif

  #
  # Simple Frame Buffer
  #
!if $(DEVICE_MODEL) == 0
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1344
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2992
!elseif $(DEVICE_MODEL) == 1
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1280
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2856
!elseif $(DEVICE_MODEL) == 2
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2424
!elseif $(DEVICE_MODEL) == 3
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2424
!elseif $(DEVICE_MODEL) == 4
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|2076
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2152
!endif
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|caimitoPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries
  #
  KeypadDeviceLib|caimitoPkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

[Components]
  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
