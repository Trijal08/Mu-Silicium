/**
  Copyright (c) 2026, Trijal Saha. All rights reserved.

  Apple SoC GPIO / pin controller driver. Produces the GPIO protocol used by
  the keypad stack. Modeled on SamsungPkg/Drivers/GpioDxe, but Apple pin
  controllers use one 32-bit register per pin (base + 4 * pin) with the pin
  level in bit 0, rather than the Samsung con/dat/pull bank layout.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationHelperLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/GpioLib.h>
#include <Library/IoLib.h>

#include <Protocol/EFIGpio.h>

//
// Global Variables
//
STATIC EFI_GPIO_CONTROLLER *mControllers;
STATIC UINT8                mControllerCount;

/**
  Return the pin controller selected by BankNumber, or NULL if out of range.
  For Apple, the GPIO protocol's "BankNumber" selects the pin controller and
  "Pin" is the flat pin index within it; "BankId" is unused.
**/
STATIC
EFI_GPIO_CONTROLLER *
GetController (
  IN UINT8  Number)
{
  if (Number >= mControllerCount) {
    return NULL;
  }

  return &mControllers[Number];
}

EFI_STATUS
EFIAPI
GpioGetState (
  IN  EFI_GPIO_BANK_ID  BankId,
  IN  UINT8             BankNumber,
  IN  UINT8             Pin,
  OUT BOOLEAN          *State)
{
  EFI_GPIO_CONTROLLER *Controller;
  UINT32               Value;

  // Get Pin Controller
  Controller = GetController (BankNumber);
  if (Controller == NULL) {
    return EFI_NOT_FOUND;
  }

  // Verify GPIO Pin
  if (Pin >= Controller->PinCount) {
    return EFI_INVALID_PARAMETER;
  }

  // Read the pin's register and pass its level (bit 0)
  Value  = MmioRead32 ((UINTN)Controller->Address + APPLE_GPIO_REG (Pin));
  *State = !!(Value & APPLE_GPIO_DATA);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GpioSetState (
  IN EFI_GPIO_BANK_ID  BankId,
  IN UINT8             BankNumber,
  IN UINT8             Pin,
  IN BOOLEAN          *Enable)
{
  EFI_GPIO_CONTROLLER *Controller;
  UINTN                Register;

  // Get Pin Controller
  Controller = GetController (BankNumber);
  if (Controller == NULL) {
    return EFI_NOT_FOUND;
  }

  // Verify GPIO Pin
  if (Pin >= Controller->PinCount) {
    return EFI_INVALID_PARAMETER;
  }

  // Drive the output level (bit 0)
  Register = (UINTN)Controller->Address + APPLE_GPIO_REG (Pin);
  if ((Enable != NULL) && *Enable) {
    MmioOr32 (Register, APPLE_GPIO_DATA);
  } else {
    MmioAnd32 (Register, ~APPLE_GPIO_DATA);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GpioSetFunction (
  IN EFI_GPIO_BANK_ID  BankId,
  IN UINT8             BankNumber,
  IN UINT8             Pin,
  IN EFI_GPIO_FUNCTION Function)
{
  EFI_GPIO_CONTROLLER *Controller;
  UINTN                Register;

  // Verify GPIO Pin Function
  if (Function >= FUNCTION_NUM) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Pin Controller
  Controller = GetController (BankNumber);
  if (Controller == NULL) {
    return EFI_NOT_FOUND;
  }

  // Verify GPIO Pin
  if (Pin >= Controller->PinCount) {
    return EFI_INVALID_PARAMETER;
  }

  // Program the pin mode (input vs output)
  Register = (UINTN)Controller->Address + APPLE_GPIO_REG (Pin);
  if (Function == FUNCTION_OUTPUT) {
    MmioAndThenOr32 (Register, ~(UINT32)(APPLE_GPIO_MODE | APPLE_GPIO_INPUT_ENABLE), APPLE_GPIO_MODE_OUT);
  } else {
    MmioAndThenOr32 (Register, ~(UINT32)APPLE_GPIO_MODE, APPLE_GPIO_MODE_IN | APPLE_GPIO_INPUT_ENABLE);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GpioSetPull (
  IN EFI_GPIO_BANK_ID   BankId,
  IN UINT8              BankNumber,
  IN UINT8              Pin,
  IN EFI_GPIO_PULL_MODE Pull)
{
  EFI_GPIO_CONTROLLER *Controller;
  UINTN                Register;
  UINT32               PullValue;

  // Verify GPIO Pin Pull
  if (Pull >= PULL_NUM) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Pin Controller
  Controller = GetController (BankNumber);
  if (Controller == NULL) {
    return EFI_NOT_FOUND;
  }

  // Verify GPIO Pin
  if (Pin >= Controller->PinCount) {
    return EFI_INVALID_PARAMETER;
  }

  // Translate to the Apple pull field
  switch (Pull) {
    case PULL_DOWN:
      PullValue = APPLE_GPIO_PULL_DOWN;
      break;
    case PULL_UP:
      PullValue = APPLE_GPIO_PULL_UP;
      break;
    default:
      PullValue = APPLE_GPIO_PULL_OFF;
      break;
  }

  Register = (UINTN)Controller->Address + APPLE_GPIO_REG (Pin);
  MmioAndThenOr32 (Register, ~(UINT32)APPLE_GPIO_PULL, PullValue);

  return EFI_SUCCESS;
}

STATIC EFI_GPIO_PROTOCOL mGpio = {
  GpioGetState,
  GpioSetState,
  GpioSetFunction,
  GpioSetPull
};

EFI_STATUS
EFIAPI
InitGpio (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Get Pin Controllers
  GetGpioControllerData (&mControllers, &mControllerCount);

  // Verify Controller Count
  if (mControllerCount == 0) {
    return EFI_UNSUPPORTED;
  }

  // Map every Pin Controller's MMIO
  for (UINT8 i = 0; i < mControllerCount; i++) {
    Status = MapMemoryRegion (mControllers[i].Address, mControllers[i].Size, EfiMemoryMappedIO);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Map GPIO Controller: 0x%llx!\n", mControllers[i].Address));
      return Status;
    }
  }

  // Register GPIO Protocol
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gEfiGpioProtocolGuid, EFI_NATIVE_INTERFACE, &mGpio);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register GPIO Protocol!\n"));
    return Status;
  }

  return EFI_SUCCESS;
}
