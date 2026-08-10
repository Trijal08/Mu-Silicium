/**
  Copyright (c) 2026, Trijal Saha. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _APPLE_GPIO_LIB_H_
#define _APPLE_GPIO_LIB_H_

#include <Uefi.h>

//
// Apple SoC pin controller register layout (one 32-bit register per pin).
// Matches drivers/pinctrl/pinctrl-apple-gpio.c in mainline Linux.
//
#define APPLE_GPIO_REG(Pin)        (4 * (Pin))       // Per-pin config/state register
#define APPLE_GPIO_DATA            BIT0              // Pin level (input or output)
#define APPLE_GPIO_MODE            (0x7 << 1)        // Bits [3:1]
#define APPLE_GPIO_MODE_OUT        (1 << 1)
#define APPLE_GPIO_MODE_IN         (7 << 1)          // Input, IRQ disabled
#define APPLE_GPIO_PERIPH          (0x3 << 5)        // Bits [6:5]
#define APPLE_GPIO_PULL            (0x3 << 7)        // Bits [8:7]
#define APPLE_GPIO_PULL_OFF        (0 << 7)
#define APPLE_GPIO_PULL_DOWN       (1 << 7)
#define APPLE_GPIO_PULL_UP_STRONG  (2 << 7)
#define APPLE_GPIO_PULL_UP         (3 << 7)
#define APPLE_GPIO_INPUT_ENABLE    BIT9

//
// One entry per Apple pin controller instance. Pins are flat within a
// controller (0 .. PinCount - 1); the EFI GPIO protocol's "BankNumber"
// selects the controller and "Pin" is the flat pin index.
//
typedef struct {
  EFI_PHYSICAL_ADDRESS    Address;   // Pin controller MMIO base
  UINTN                   Size;      // MMIO region size to map
  UINT32                  PinCount;  // Number of pins (apple,npins)
} EFI_GPIO_CONTROLLER;

/**
  Returns the platform's Apple pin controller list.

  @param[out] Data   The GPIO controller array.
  @param[out] Count  The number of GPIO controllers.
**/
VOID
GetGpioControllerData (
  OUT EFI_GPIO_CONTROLLER **Data,
  OUT UINT8                *Count
  );

#endif /* _APPLE_GPIO_LIB_H_ */
