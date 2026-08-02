/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>

#include "PlatformRegisters.h"

VOID
DisableWatchdogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WatchdogTimerCl0Region;
  EFI_MEMORY_REGION_DESCRIPTOR WatchdogTimerCl1Region;

  // 1. Locate and disable Cluster 0 Watchdog independently
  Status = LocateMemoryRegionByName ("Watchdog Timer CL0", &WatchdogTimerCl0Region);
  if (!EFI_ERROR (Status)) {
    MmioAnd32 (WatchdogTimerCl0Region.Address, ~WATCHDOG_ENABLE);
  }

  // 2. Locate and disable Cluster 1 Watchdog independently
  Status = LocateMemoryRegionByName ("Watchdog Timer CL1", &WatchdogTimerCl1Region);
  if (!EFI_ERROR (Status)) {
    MmioAnd32 (WatchdogTimerCl1Region.Address, ~WATCHDOG_ENABLE);
  }
}

VOID
EnableFrameBufferWrites ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR DrmDeconRegion;

  // Locate "DRM Decon" Memory Region
  Status = LocateMemoryRegionByName ("DRM Decon", &DrmDeconRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Configure DRM Decon
  for (UINT16 Length = 0; Length < DrmDeconRegion.Length; Length += 0x1000) {
    MmioWrite32 (DrmDeconRegion.Address + Length + HW_SW_TRIG_CONTROL, 0x3061);
  }
}

VOID
PlatformInitialize ()
{
  // Disable Watchdog Timer
  DisableWatchdogTimer ();

  // Enable Frame Buffer Writes
  EnableFrameBufferWrites ();
}
