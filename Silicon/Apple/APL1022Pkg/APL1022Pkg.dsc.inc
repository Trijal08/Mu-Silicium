##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  #
  # Timer
  #
  USE_PHYSICAL_TIMER                         = 1

  #
  # KEP
  #
  HAS_CNTFRQ_EL0_EMPTY_ERRATA                = 0

!include ApplePkg/ApplePkg.dsc.inc

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -march=armv8-a+crypto -mcpu=apple-a9

[PcdsFixedAtBuild]
  #
  # CPU
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|2
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1

  #
  # GIC
  #
  gApplePkgTokenSpaceGuid.PcdAicDistributorBase|0x20e100000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Apple (TM) A9 (TSMC Malta) @ 1.85 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"APL1022"

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|APL1022Pkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # IO Libraries
  #
  GpioLib|APL1022Pkg/Library/GpioLib/GpioLib.inf

  #
  # UART Serial Libraries
  #
  # SerialPortLib|APL1022Pkg/Library/SerialPortLib/SerialPortLib.inf

  #
  # Storage Libraries
  #
  # PlatformUfsLib|APL1022Pkg/Library/PlatformUfsLib/PlatformUfsLib.inf
