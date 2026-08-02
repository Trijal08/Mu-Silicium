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
  *_*_AARCH64_CC_FLAGS = -mcpu=cortex-x4 -march=armv9.2-a+aes+bf16+bti+crc+crypto+f32mm+f64mm+flagm+fp16+fp16fml+fp8+gcs+i8mm+ite+ls64+lse+lse128+lut+memtag+pauth+pauth-lr+predres+profile+ras+rasv2+rcpc+rcpc3+rdm+rng+sb+sha2+sha3+sm4+sme+sme-f64f64+sme-fa64+sme-i16i64+sme-lutv2+sme2+sme2p1+ssbs+ssve-fp8fma+sve-bitperm+sve2-aes+sve2-bitperm+sve2-sha3+sve2-sm4+sve2p1+wfxt

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
  # UART Serial Libraries
  #
  SerialPortLib|S5P9875Pkg/Library/SerialPortLib/SerialPortLib.inf

