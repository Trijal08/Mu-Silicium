/**
  Copyright (c) 2026, Trijal Saha. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/GpioLib.h>

//
// Apple S8003 (APL1022 / A9) pin controllers.
//
// Bases and pin counts come from the mainline device tree
// (arch/arm64/boot/dts/apple/s8000.dtsi, "apple,s8000-pinctrl"):
//   pinctrl@20f100000  apple,npins = 0xd0 (208)  -> pinctrl_ap  (buttons live here)
//   pinctrl@2100f0000  apple,npins = 0x2a (42)   -> pinctrl_aop
//
STATIC
EFI_GPIO_CONTROLLER
gGpioControllers[] = {
  // AP pin controller
  {
    .Address  = 0x20F100000,
    .Size     = 0x100000,
    .PinCount = 0xD0
  },

  // AOP pin controller
  {
    .Address  = 0x2100F0000,
    .Size     = 0x100000,
    .PinCount = 0x2A
  }
};

VOID
GetGpioControllerData (
  OUT EFI_GPIO_CONTROLLER **Data,
  OUT UINT8                *Count)
{
  // Pass Data
  *Data  = gGpioControllers;
  *Count = ARRAY_SIZE (gGpioControllers);
}
