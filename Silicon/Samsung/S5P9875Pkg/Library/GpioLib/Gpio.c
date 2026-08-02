#include <Library/GpioLib.h>

//
// GPIO Controller Data for the zumapro SoC (Tensor G4).
//
// Bank Layout comes from the Mainline Linux Pin Controller Data
// (drivers/pinctrl/samsung/pinctrl-exynos-arm64.c, zuma_pin_*) and
// arch/arm64/boot/dts/exynos/google/zumapro.dtsi.
//
// The GSA Pin Controllers (gsacore0-3, gsactrl) are intentionally not
// listed: they live in the Security Core Power Domain and are not
// accessible from the Application Processor.
//
STATIC
EFI_GPIO_CONTROLLER_DATA
gGpioControllers[] = {
  // Pinctrl Address - Bank ID, Bank Number, Bank Offset

  // PERIC0
  {
    .Address    = 0x10840000,
    {
      {
        .Id     = BANK_ID_P,
        .Number = 0,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 1,
        .Offset = 0x20
      },
      {
        .Id     = BANK_ID_P,
        .Number = 2,
        .Offset = 0x40
      },
      {
        .Id     = BANK_ID_P,
        .Number = 3,
        .Offset = 0x60
      },
      {
        .Id     = BANK_ID_P,
        .Number = 4,
        .Offset = 0x80
      },
      {
        .Id     = BANK_ID_P,
        .Number = 5,
        .Offset = 0xA0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 6,
        .Offset = 0xC0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 7,
        .Offset = 0xE0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 8,
        .Offset = 0x100
      },
      {
        .Id     = BANK_ID_P,
        .Number = 9,
        .Offset = 0x120
      },
      {
        .Id     = BANK_ID_P,
        .Number = 10,
        .Offset = 0x140
      },
      {
        .Id     = BANK_ID_P,
        .Number = 11,
        .Offset = 0x160
      },
      {
        .Id     = BANK_ID_P,
        .Number = 12,
        .Offset = 0x180
      },
      {
        .Id     = BANK_ID_P,
        .Number = 13,
        .Offset = 0x1A0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 14,
        .Offset = 0x1C0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 15,
        .Offset = 0x1E0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 17,
        .Offset = 0x200
      },
      {
        .Id     = BANK_ID_P,
        .Number = 16,
        .Offset = 0x220
      }
    }
  },

  // PERIC1
  {
    .Address    = 0x10C40000,
    {
      {
        .Id     = BANK_ID_P,
        .Number = 19,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_P,
        .Number = 20,
        .Offset = 0x20
      },
      {
        .Id     = BANK_ID_P,
        .Number = 21,
        .Offset = 0x40
      },
      {
        .Id     = BANK_ID_P,
        .Number = 24,
        .Offset = 0x60
      },
      {
        .Id     = BANK_ID_P,
        .Number = 22,
        .Offset = 0x80
      },
      {
        .Id     = BANK_ID_P,
        .Number = 23,
        .Offset = 0xA0
      }
    }
  },

  // HSI1
  {
    .Address    = 0x12040000,
    {
      {
        .Id     = BANK_ID_H,
        .Number = 0,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_H,
        .Number = 1,
        .Offset = 0x20
      },
      {
        .Id     = BANK_ID_H,
        .Number = 2,
        .Offset = 0x40
      }
    }
  },

  // HSI2
  {
    .Address    = 0x13040000,
    {
      {
        .Id     = BANK_ID_H,
        .Number = 3,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_H,
        .Number = 4,
        .Offset = 0x20
      }
    }
  },

  // HSI2UFS
  {
    .Address    = 0x13060000,
    {
      {
        .Id     = BANK_ID_H,
        .Number = 5,
        .Offset = 0x0
      }
    }
  },

  // GPIO_CUSTOM_ALIVE
  {
    .Address    = 0x15060000,
    {
      {
        .Id     = BANK_ID_N,
        .Number = 0,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_N,
        .Number = 1,
        .Offset = 0x20
      },
      {
        .Id     = BANK_ID_N,
        .Number = 2,
        .Offset = 0x40
      },
      {
        .Id     = BANK_ID_N,
        .Number = 3,
        .Offset = 0x60
      },
      {
        .Id     = BANK_ID_N,
        .Number = 4,
        .Offset = 0x80
      },
      {
        .Id     = BANK_ID_N,
        .Number = 5,
        .Offset = 0xA0
      },
      {
        .Id     = BANK_ID_N,
        .Number = 6,
        .Offset = 0xC0
      },
      {
        .Id     = BANK_ID_N,
        .Number = 7,
        .Offset = 0xE0
      },
      {
        .Id     = BANK_ID_N,
        .Number = 8,
        .Offset = 0x100
      },
      {
        .Id     = BANK_ID_N,
        .Number = 9,
        .Offset = 0x120
      }
    }
  },

  // GPIO_ALIVE
  {
    .Address    = 0x154D0000,
    {
      {
        .Id     = BANK_ID_A,
        .Number = 0,
        .Offset = 0x0
      },
      {
        .Id     = BANK_ID_A,
        .Number = 1,
        .Offset = 0x20
      },
      {
        .Id     = BANK_ID_A,
        .Number = 2,
        .Offset = 0x40
      },
      {
        .Id     = BANK_ID_A,
        .Number = 3,
        .Offset = 0x60
      },
      {
        .Id     = BANK_ID_A,
        .Number = 4,
        .Offset = 0x80
      },
      {
        .Id     = BANK_ID_A,
        .Number = 6,
        .Offset = 0xA0
      },
      {
        .Id     = BANK_ID_A,
        .Number = 7,
        .Offset = 0xC0
      },
      {
        .Id     = BANK_ID_A,
        .Number = 8,
        .Offset = 0xE0
      },
      {
        .Id     = BANK_ID_A,
        .Number = 9,
        .Offset = 0x100
      },
      {
        .Id     = BANK_ID_A,
        .Number = 10,
        .Offset = 0x120
      }
    }
  },

  // FAR_ALIVE
  {
    .Address    = 0x154E0000,
    {
      {
        .Id     = BANK_ID_A,
        .Number = 5,
        .Offset = 0x0
      }
    }
  }
};

VOID
GetGpioControllerData (
  OUT EFI_GPIO_CONTROLLER_DATA **Data,
  OUT UINT8                     *Count)
{
  // Pass Data
  *Data  = gGpioControllers;
  *Count = ARRAY_SIZE (gGpioControllers);
}
