#include <Library/GpioBankLib.h>

STATIC
EFI_GPIO_BANK
mGpioBanks[] = {
  //
  // PERIC0
  //
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 0,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 1,
    .Offset  = 0x20
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 2,
    .Offset  = 0x40
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 3,
    .Offset  = 0x60
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 4,
    .Offset  = 0x80
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 5,
    .Offset  = 0xA0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 6,
    .Offset  = 0xC0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 7,
    .Offset  = 0xE0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 8,
    .Offset  = 0x100
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 9,
    .Offset  = 0x120
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 10,
    .Offset  = 0x140
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 11,
    .Offset  = 0x160
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 12,
    .Offset  = 0x180
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 13,
    .Offset  = 0x1A0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 14,
    .Offset  = 0x1C0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 15,
    .Offset  = 0x1E0
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 17,
    .Offset  = 0x200
  },
  {
    .CtrlNum = 0,
    .Id      = BANK_ID_P,
    .Number  = 16,
    .Offset  = 0x220
  },
  //
  // PERIC1
  //
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 19,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 20,
    .Offset  = 0x20
  },
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 21,
    .Offset  = 0x40
  },
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 24,
    .Offset  = 0x60
  },
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 22,
    .Offset  = 0x80
  },
  {
    .CtrlNum = 1,
    .Id      = BANK_ID_P,
    .Number  = 23,
    .Offset  = 0xA0
  },
  //
  // HSI1
  //
  {
    .CtrlNum = 2,
    .Id      = BANK_ID_H,
    .Number  = 0,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 2,
    .Id      = BANK_ID_H,
    .Number  = 1,
    .Offset  = 0x20
  },
  {
    .CtrlNum = 2,
    .Id      = BANK_ID_H,
    .Number  = 2,
    .Offset  = 0x40
  },
  //
  // HSI2
  //
  {
    .CtrlNum = 3,
    .Id      = BANK_ID_H,
    .Number  = 3,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 3,
    .Id      = BANK_ID_H,
    .Number  = 4,
    .Offset  = 0x20
  },
  //
  // HSI2UFS
  //
  {
    .CtrlNum = 4,
    .Id      = BANK_ID_H,
    .Number  = 5,
    .Offset  = 0x0
  },
  //
  // GPIO_CUSTOM_ALIVE
  //
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 0,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 1,
    .Offset  = 0x20
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 2,
    .Offset  = 0x40
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 3,
    .Offset  = 0x60
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 4,
    .Offset  = 0x80
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 5,
    .Offset  = 0xA0
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 6,
    .Offset  = 0xC0
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 7,
    .Offset  = 0xE0
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 8,
    .Offset  = 0x100
  },
  {
    .CtrlNum = 5,
    .Id      = BANK_ID_N,
    .Number  = 9,
    .Offset  = 0x120
  },
  //
  // GPIO_ALIVE
  //
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 0,
    .Offset  = 0x0
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 1,
    .Offset  = 0x20
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 2,
    .Offset  = 0x40
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 3,
    .Offset  = 0x60
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 4,
    .Offset  = 0x80
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 6,
    .Offset  = 0xA0
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 7,
    .Offset  = 0xC0
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 8,
    .Offset  = 0xE0
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 9,
    .Offset  = 0x100
  },
  {
    .CtrlNum = 6,
    .Id      = BANK_ID_A,
    .Number  = 10,
    .Offset  = 0x120
  },
  //
  // FAR_ALIVE
  //
  {
    .CtrlNum = 7,
    .Id      = BANK_ID_A,
    .Number  = 5,
    .Offset  = 0x0
  }
};

VOID
GetGpioBanks (
  OUT EFI_GPIO_BANK **Bank,
  OUT UINT8          *Count)
{
  // Pass Data
  *Bank  = mGpioBanks;
  *Count = ARRAY_SIZE (mGpioBanks);
}