/** @file
  ResetSystemLib for Apple A-series SoCs (e.g. S8003 / APL1022).

  Apple silicon booted via pongoOS runs with no PSCI secure monitor, so the
  stock ArmPsciResetSystemLib (SMC SYSTEM_RESET / SYSTEM_OFF) does nothing.
  Reboot is instead performed through the SoC watchdog timer, exactly as
  pongoOS does it. Power-off requires the antigua PMIC over I2C.

  Copyright (c) 2026, Trijal Saha. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "Wdt.h"

//
// Global Variables
//
STATIC EFI_MEMORY_REGION_DESCRIPTOR  mWdtRegion = {0};

VOID
EFIAPI
ResetCold ()
{
  // Verify Watchdog Timer Memory Region Presence
  if (mWdtRegion.Address == 0 && mWdtRegion.Length == 0) {
    DEBUG ((EFI_D_WARN, "%a: The \"Watchdog Timer\" Memory Region isn't Mapped!\n", __FUNCTION__));
    return;
  }

  // Force an immediate system watchdog reset (same sequence as pongoOS)
  MmioWrite32 (mWdtRegion.Address + WDT_CHIP_CTL, 0x0);              // Disable chip WDT
  MmioWrite32 (mWdtRegion.Address + WDT_SYS_CTL,  0x0);              // Disable system WDT
  MmioWrite32 (mWdtRegion.Address + WDT_SYS_RST,  0x1);             // Arm immediate reset
  MmioWrite32 (mWdtRegion.Address + WDT_SYS_CTL,  WDT_SYS_CTL_ENABLE); // Enable system WDT
  MmioWrite32 (mWdtRegion.Address + WDT_SYS_TMR,  0x0);             // Zero the counter -> fire now
}

VOID
EFIAPI
ResetWarm ()
{
  // Do Cold Reboot instead
  ResetCold ();
}

VOID
EFIAPI
ResetShutdown ()
{
  //
  // TODO: Real power-off drives the antigua PMIC (apple,antigua-pmic, I2C addr
  // 0x74 on i2c@20a110000, 16-bit register / 8-bit value) via the Apple PASEMI
  // I2C controller. The PMIC power-off register is not documented in the
  // available references (mainline leaves it "to be described"), so rather than
  // risk an unverified PMIC write, map power-off to a cold reboot for now.
  //
  DEBUG ((EFI_D_WARN, "%a: Power-off not implemented; issuing a cold reboot instead.\n", __FUNCTION__));
  ResetCold ();
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID  *ResetData)
{
  // Do Cold Reboot instead
  ResetCold ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID           *ResetData)
{
  // Check Reset Type
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;

    case EfiResetCold:
      ResetCold ();
      break;

    case EfiResetShutdown:
      ResetShutdown ();
      return;

    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      return;

    default:
      return;
  }
}

EFI_STATUS
EFIAPI
ResetSystemLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Locate Watchdog Timer Memory Region
  Status = LocateMemoryRegionByName ("Watchdog Timer", &mWdtRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"Watchdog Timer\" Memory Region! Status = %r\n", Status));
  }

  return EFI_SUCCESS;
}
