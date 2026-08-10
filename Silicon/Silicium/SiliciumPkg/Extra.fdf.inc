  #
  # Remaining UEFI Drivers
  #
  INF MdeModulePkg/Universal/Acpi/BootGraphicsResourceTableDxe/BootGraphicsResourceTableDxe.inf
  INF MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf

  #
  # UEFI Applications
  #
  INF MdeModulePkg/Application/BootManagerMenuApp/BootManagerMenuApp.inf
  INF PcBdsPkg/MsBootPolicy/MsBootPolicy.inf
  INF SiliciumPkg/Applications/UFPLoader/UFPLoader.inf
  INF SiliciumPkg/Applications/UFP/ufpdevicefw.inf

  #
  # RAW Files
  #
!if $(ENABLE_SECUREBOOT) == 1
  FILE FREEFORM = PCD (gSiliciumPkgTokenSpaceGuid.PcdSiPolicyFile) {
    SECTION RAW = SiliciumPkg/Include/Resources/SecureBoot/SiPolicy.p7b
    SECTION UI = "SiPolicy.p7b"
  }
!endif

  # Full-size boot logo. Stored under a FIXED GUID (== the default value of
  # gOemPkgTokenSpaceGuid.PcdLogoFile) rather than PCD(PcdLogoFile), so a board
  # may repoint PcdLogoFile at the scaled logo below without moving this file
  # (which would collide with it). Keep this GUID in sync with OemPkg.dec.
  FILE FREEFORM = 7BB28B99-61BB-11D5-9A5D-0090273FC14D {
    SECTION RAW = SiliciumPkg/Include/Resources/BootLogo.bmp
    SECTION UI = "BootLogo.bmp"
  }

  # Scaled-down boot logo, for panels too small for the full logo to satisfy the
  # MsGraphicsPkg 40% size cap. A board opts in by setting
  # gOemPkgTokenSpaceGuid.PcdLogoFile to this file's GUID (PcdScaledLogoFile).
  FILE FREEFORM = PCD (gSiliciumPkgTokenSpaceGuid.PcdScaledLogoFile) {
    SECTION RAW = SiliciumPkg/Include/Resources/BootLogoSmall.bmp
    SECTION UI = "BootLogoSmall.bmp"
  }

  FILE FREEFORM = PCD (gSiliciumPkgTokenSpaceGuid.PcdNoBootOSFile) {
    SECTION RAW = SiliciumPkg/Include/Resources/NoBootOS.bmp
    SECTION UI = "NoBootOS.bmp"
  }

  FILE FREEFORM = PCD (gSiliciumPkgTokenSpaceGuid.PcdMassStorageBmpFile) {
    SECTION RAW = SiliciumPkg/Include/Resources/MassStorage.bmp
    SECTION UI = "MassStorage.bmp"
  }
