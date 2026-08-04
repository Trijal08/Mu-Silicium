#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  {"UEFI FD",            0xA0011000, 0x00601000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI Stack",         0xA0612000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap",           0x890000000ULL, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"Display Reserved",   0xFAC00000, ALIGN_VALUE ((FixedPcdGet32 (PcdFrameBufferWidth) * FixedPcdGet32 (PcdFrameBufferHeight)) * 4, SIZE_4KB), AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},

  // iRAM Regions
  {"IRAM",               0x02020000, 0x00018000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},
  {"NS_IRAM",            0x02038000, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  // SRAM Regions
  {"APM SRAM",           0x15700000, 0x00058000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   DEVICE},

  // Reserved Memory Regions (Carveouts from the zumapro Device Tree reserved-memory Node)
  {"pKVM Firmware",      0x8B000000, 0x000BC000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"ECT Binary",         0x90000000, 0x00060000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"GSA Secure",         0x90200000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"GXP/TPU Firmware",   0x91300000, 0x02D00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"AOC Firmware",       0x94000000, 0x03000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"XHCI DMA",           0x97000000, 0x00400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Secure DRAM",        0xB6200000, 0x09E00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Secure Log",         0xC3000000, 0x00080000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Debug Logs",         0xD8100000, 0x05F20000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Secure Page Table",  0xE0000000, 0x03800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"CP Reserved 1",      0xE8000000, 0x02000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"CP Reserved",        0xEA400000, 0x00C00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"CP MSI",             0xF6200000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"ABL Reserved",       0xF8800000, 0x01000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Ramoops / DSS Logs", 0xFD3F0000, 0x0040F000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"Bootloader Log",     0xFD800000, 0x00100000, AddMem, MEM_RES, SYS_MEM_CAP, BsData, WRITE_BACK},

  // Register Regions
  {"Chip Info",          0x10000000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Watchdog Timer CL0", 0x10060000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Watchdog Timer CL1", 0x10070000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICD",               0x10400000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICR",               0x10440000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PMU",                0x15460000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG HSI0",        0x11020000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  //
  // The USI SW_CONF Registers that select UART, SPI or I2C on each USI Block
  // live in these System Register Blocks, not in the USI Blocks themselves, so
  // UsiLib's Addresses land here. HSI0 above already covers the SPI Blocks that
  // the Touch Controller and its Neighbours hang off.
  //
  {"CMU PERIC0",         0x10800000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG PERIC0",      0x10820000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG PERIC1",      0x10C20000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG APM",         0x15420000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  //
  // Doorbell towards the Always On Processor. Its Static RAM is already Mapped
  // further up as "APM SRAM", and both are needed to Switch a Power Rail.
  //
  {"APM Mailbox",        0x15110000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

  //
  // SPI Controller of USI 20, which carries the Synaptics Touch Controller.
  //
  {"SPI Touch",          0x111D0000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"DRM Decon",          0x19470000, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UART",               0x10870000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"Clock Controller",   0x11000000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG UFS",         0x13020000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS HCI",            0x13200000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS UniPro",         0x13280000, 0x00008000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"CMU TOP",            0x26040000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
