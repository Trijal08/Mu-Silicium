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

  #
  # Replaces the Text Boot Menu with Simple Init's Graphical one.
  #
  # A Platform opting in also has to add "Common/SimpleInit" to both
  # PackagesPath and GetRequiredSubmodules in its DeviceBuild.py, and guard its
  # own Firmware Volume Entry the same way this Platform's does. Platforms that
  # never set this Define are unaffected.
  #
  # Note that Simple Init is LGPL v3 while the rest of this Tree is
  # BSD-2-Clause-Patent.
  #
  USE_SIMPLE_INIT                = 1

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
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x806404000
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

#
# Simple Init. Its own Include resolves the Graphics Stack it needs, so the
# Simple Init Root has to be on the Packages Path.
#
!if $(USE_SIMPLE_INIT) == 1
!include SimpleInit.inc

[LibraryClasses]
  #
  # Classes Simple Init needs that its own Include does not resolve.
  #
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf

[PcdsFixedAtBuild]
  #
  # Interface Scale, which Simple Init sizes its Icons, Text and Padding from.
  #
  # Simple Init Multiplies Real Lengths by this Number, and its own Default of Two
  # Hundred is only an Unopinionated Fallback rather than a Target. Feeding it each
  # Panel's Full Physical Density came out too Large and Dropping it near that
  # Default came out too Small. The Figure for the Largest of these Panels was
  # Settled by Eye on the Hardware and lands around Three Quarters of its Real
  # Density. The rest Keep their Proportion to it and are Extrapolated rather than
  # Tried, so they may want Adjusting on their own Hardware.
  #
  # This can also be Overridden at Runtime, without Rebuilding, through the
  # "gui.dpi_force" Setting or a "dpi_force" Argument on the Command Line, which
  # is the Easier Way to Settle on a Number that Looks right.
  #
  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|200

  #
  # Point the Boot Manager Menu at Simple Init instead of the Text Menu. This
  # is the FILE_GUID of SimpleInit/src/main/SimpleInitMain.inf.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0xBB, 0xB2, 0x77, 0x6D, 0xEB, 0x69, 0xAB, 0x42, 0xBE, 0xCF, 0x4F, 0x40, 0xC8, 0x95, 0x68, 0xC3 }
!endif
