/**
  Copyright (c) 2024, Google LLC. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/UsiLib.h>

//
// Each USI Block on this SoC muxes one Controller between UART, SPI and I2C.
// "UsiAddress" is the SW_CONF Register that selects which of the three is
// wired up, and it lives in a System Register Block rather than in the USI
// Block itself: the Device Tree spells it "samsung,sysreg = <&sysreg_X off>",
// so the Address below is that Block's Base plus the Offset. UsiDxe writes
// BIT0, BIT1 or BIT2 there for UART, SPI or I2C, which matches the USIv2
// Encoding in the Kernel's exynos-usi Driver.
//
// The Bus Numbers are the Instance Numbers the Device Tree labels its Nodes
// with (serial_N, spi_N, hsi2c_N), not the Order of this Table. They are not
// contiguous and they do not agree across the three Bus Types, which is why
// each one is recorded separately. MAX_UINT8 means the Device Tree exposes no
// Node of that Type on the Block.
//
// Of Note: Entry 0 is the Debug UART this Platform already prints on, and
// Entry 17 is the SPI the Synaptics Touch Controller sits on.
//
STATIC
EFI_USI_DATA
gUsiData[] = {
  [0] = {
    .UsiAddress        = 0x10821020,  // usi_uart: sysreg_peric0 + 0x1020
    .ControllerAddress = 0x10870000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = MAX_UINT8,
    .UartBus           = 0
  },
  [1] = {
    .UsiAddress        = 0x10821000,  // usi1: sysreg_peric0 + 0x1000
    .ControllerAddress = 0x10880000,
    .I2cBus            = 1,
    .SpiBus            = 1,
    .UartBus           = 1
  },
  [2] = {
    .UsiAddress        = 0x10821004,  // usi2: sysreg_peric0 + 0x1004
    .ControllerAddress = 0x10890000,
    .I2cBus            = 2,
    .SpiBus            = 2,
    .UartBus           = 2
  },
  [3] = {
    .UsiAddress        = 0x10821008,  // usi3: sysreg_peric0 + 0x1008
    .ControllerAddress = 0x108A0000,
    .I2cBus            = 3,
    .SpiBus            = 3,
    .UartBus           = 3
  },
  [4] = {
    .UsiAddress        = 0x1082100C,  // usi4: sysreg_peric0 + 0x100C
    .ControllerAddress = 0x108B0000,
    .I2cBus            = 4,
    .SpiBus            = 4,
    .UartBus           = 4
  },
  [5] = {
    .UsiAddress        = 0x10821010,  // usi5: sysreg_peric0 + 0x1010
    .ControllerAddress = 0x108C0000,
    .I2cBus            = 5,
    .SpiBus            = 5,
    .UartBus           = 5
  },
  [6] = {
    .UsiAddress        = 0x10821014,  // usi6: sysreg_peric0 + 0x1014
    .ControllerAddress = 0x108D0000,
    .I2cBus            = 6,
    .SpiBus            = 6,
    .UartBus           = 6
  },
  [7] = {
    .UsiAddress        = 0x10821028,  // usi14: sysreg_peric0 + 0x1028
    .ControllerAddress = 0x10980000,
    .I2cBus            = 14,
    .SpiBus            = 14,
    .UartBus           = 14
  },
  [8] = {
    .UsiAddress        = 0x10C21000,  // usi0: sysreg_peric1 + 0x1000
    .ControllerAddress = 0x10C80000,
    .I2cBus            = 0,
    .SpiBus            = 0,
    .UartBus           = MAX_UINT8
  },
  [9] = {
    .UsiAddress        = 0x10C21004,  // usi9: sysreg_peric1 + 0x1004
    .ControllerAddress = 0x10C90000,
    .I2cBus            = 9,
    .SpiBus            = 9,
    .UartBus           = 9
  },
  [10] = {
    .UsiAddress        = 0x10C21008,  // usi10: sysreg_peric1 + 0x1008
    .ControllerAddress = 0x10CA0000,
    .I2cBus            = 10,
    .SpiBus            = 10,
    .UartBus           = 10
  },
  [11] = {
    .UsiAddress        = 0x10C2100C,  // usi11: sysreg_peric1 + 0x100C
    .ControllerAddress = 0x10CB0000,
    .I2cBus            = 11,
    .SpiBus            = 11,
    .UartBus           = 11
  },
  [12] = {
    .UsiAddress        = 0x10C21010,  // usi12: sysreg_peric1 + 0x1010
    .ControllerAddress = 0x10CC0000,
    .I2cBus            = 12,
    .SpiBus            = 12,
    .UartBus           = 12
  },
  [13] = {
    .UsiAddress        = 0x10C21014,  // usi13: sysreg_peric1 + 0x1014
    .ControllerAddress = 0x10CD0000,
    .I2cBus            = 13,
    .SpiBus            = 13,
    .UartBus           = 13
  },
  [14] = {
    .UsiAddress        = 0x10C21018,  // usi15: sysreg_peric1 + 0x1018
    .ControllerAddress = 0x10CE0000,
    .I2cBus            = 15,
    .SpiBus            = 15,
    .UartBus           = 15
  },
  [15] = {
    .UsiAddress        = 0x11021014,  // usi23: sysreg_hsi0 + 0x1014
    .ControllerAddress = 0x111B0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 18,
    .UartBus           = MAX_UINT8
  },
  [16] = {
    .UsiAddress        = 0x11021018,  // usi24: sysreg_hsi0 + 0x1018
    .ControllerAddress = 0x111C0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 19,
    .UartBus           = MAX_UINT8
  },
  [17] = {
    .UsiAddress        = 0x1102101C,  // usi20: sysreg_hsi0 + 0x101C
    .ControllerAddress = 0x111D0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 20,
    .UartBus           = MAX_UINT8
  },
  [18] = {
    .UsiAddress        = 0x11021020,  // usi21: sysreg_hsi0 + 0x1020
    .ControllerAddress = 0x111E0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 21,
    .UartBus           = MAX_UINT8
  },
  [19] = {
    .UsiAddress        = 0x11021024,  // usi22: sysreg_hsi0 + 0x1024
    .ControllerAddress = 0x111F0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 22,
    .UartBus           = MAX_UINT8
  },
  [20] = {
    .UsiAddress        = 0x154204E0,  // usi17: sysreg_apm + 0x4E0
    .ControllerAddress = 0x155C0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = MAX_UINT8,
    .UartBus           = 17
  },
  [21] = {
    .UsiAddress        = 0x154204E4,  // usi18: sysreg_apm + 0x4E4
    .ControllerAddress = 0x155D0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = MAX_UINT8,
    .UartBus           = 18
  },
  [22] = {
    .UsiAddress        = 0x154204E8,  // usi19: sysreg_apm + 0x4E8
    .ControllerAddress = 0x155E0000,
    .I2cBus            = MAX_UINT8,
    .SpiBus            = 17,
    .UartBus           = 19
  }
};

VOID
GetUsiData (
  OUT EFI_USI_DATA **Data,
  OUT UINT8         *Count)
{
  // Pass Data
  *Data  = gUsiData;
  *Count = ARRAY_SIZE (gUsiData);
}
