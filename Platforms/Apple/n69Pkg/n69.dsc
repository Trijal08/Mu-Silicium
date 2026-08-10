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
  PLATFORM_NAME                  = n69
  PLATFORM_GUID                  = 9146BB63-EE16-4238-A6C6-36472B925699
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/n69Pkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = n69Pkg/n69.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include APL1022Pkg/APL1022Pkg.dsc.inc

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
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x800000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x80000000

  #
  # UEFI Stack
  #
  # 16 KB-aligned: the A9 MMU uses a 16 KB granule, so every mapped region must
  # be 16 KB-aligned in base and length. This sits right after the FD region,
  # whose length is rounded up to 0x204000 in MemoryMapLib.
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x806004000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Apple"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"iPhone SE (1st generation, 2016)"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"n69"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"iPhone_SE_n69"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"iPhone SE (1st generation, 2016)"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|640
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1136
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Boot Logo
  #
  # The 640-wide panel is too small for the full 300x300 logo to pass the
  # MsGraphicsPkg 40% size cap, so use the pre-scaled logo instead. Pointing
  # PcdLogoFile at PcdScaledLogoFile's GUID makes *every* logo consumer fetch
  # the small file (SiliciumPkg embeds both under their fixed GUIDs).
  #
  gOemPkgTokenSpaceGuid.PcdLogoFile|{ 0xB2, 0xEC, 0x86, 0x9B, 0x7B, 0x45, 0x04, 0x4C, 0x8F, 0x2B, 0x49, 0xF7, 0xA3, 0x0F, 0xA1, 0x35 }

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|n69Pkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries
  #
  KeypadDeviceLib|n69Pkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

[Components]
  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
