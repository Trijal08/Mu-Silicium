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

!include SamsungPkg/SamsungPkg.dsc.inc

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -march=armv9.2-a+crypto+rcpc+pauth

[PcdsFixedAtBuild]
  #
  # CPU
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|3

  #
  # GIC
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x10400000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x10440000

  #
  # Alive Core
  #
  # The Power Management Chips on this SoC hang off the Always On Processor rather
  # than off any Bus the Application Processor can Address, so a Rail can only be
  # Switched by Asking that Processor. Its Static RAM is already in the Memory
  # Map as "APM SRAM".
  #
  gSamsungPkgTokenSpaceGuid.PcdAcpmSramBase|0x15700000
  gSamsungPkgTokenSpaceGuid.PcdAcpmMailboxBase|0x15110000
  gSamsungPkgTokenSpaceGuid.PcdAcpmInitDataOffset|0xA000

  #
  # RAM Manager
  #
  gSamsungPkgTokenSpaceGuid.PcdHasSecurePgtbl|TRUE
  gSamsungPkgTokenSpaceGuid.PcdHasHarx|TRUE
  gSamsungPkgTokenSpaceGuid.PcdHasDrmPlugin|TRUE

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Google Tensor (TM) G4 @ 3.10 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"S5P9875"

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|S5P9875Pkg/Library/PlatformSecLib/PlatformSecLib.inf

  #
  # IO Libraries
  #
  GpioLib|S5P9875Pkg/Library/GpioLib/GpioLib.inf

  #
  # USI Libraries
  #
  # Overrides the Null Instance that SamsungPkg.dsc.inc resolves above. Without
  # a Table UsiDxe reports no USI at all and fails with EFI_UNSUPPORTED, which
  # also leaves HsI2cDxe with nothing to bind to.
  #
  UsiLib|S5P9875Pkg/Library/UsiLib/UsiLib.inf

  #
  # Power Management Libraries
  #
  AcpmLib|SamsungPkg/Library/AcpmLib/AcpmLib.inf

  #
  # UART Serial Libraries
  #
  SerialPortLib|S5P9875Pkg/Library/SerialPortLib/SerialPortLib.inf

  #
  # Storage Libraries
  #
  PlatformUfsLib|S5P9875Pkg/Library/PlatformUfsLib/PlatformUfsLib.inf

  #
  # Random Number Libraries
  #
  # This SoC does not answer the Samsung Crypto Manager Call that SamsungPkg's
  # own Random Number Driver uses (SMC 0xC200101C, a Vendor Service), because
  # the Firmware here is Google's rather than Samsung's. It does provide the
  # Architectural Interface instead, which its Device Tree advertises as
  # "arm,smccc-trng".
  #
  ArmTrngLib|ArmPkg/Library/ArmTrngLib/ArmTrngLib.inf

[Components]
  #
  # Random Number Generator
  #
  # The Driver itself also consumes RngLib, which must NOT be the DXE Instance
  # here: that one looks up the very Protocol this Driver produces. A Base
  # Instance is used so the Driver falls back to the Firmware Interface.
  #
  SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf {
    <LibraryClasses>
      RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf
  }
