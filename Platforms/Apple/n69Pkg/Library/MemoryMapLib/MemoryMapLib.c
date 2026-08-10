#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  // NOTE: The Apple A9 MMU only implements the 16 KB translation granule, so
  // every region here must be 16 KB-aligned in both base and length. The FD
  // region length is rounded up from the 0x201000 FD image to 0x204000, which
  // pushes the stack base to 0x806004000 (see n69.dsc PcdCPUCoresStackBase),
  // and Display Reserved is aligned to SIZE_16KB.
  {"UEFI FD",            0x805E00000ULL, 0x00204000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI Stack",         0x806004000ULL, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap",           0x840000000ULL, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"Display Reserved",   0x87EA5C000ULL, ALIGN_VALUE ((FixedPcdGet32 (PcdFrameBufferWidth) * FixedPcdGet32 (PcdFrameBufferHeight)) * 4, SIZE_16KB), AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},

  // Register Regions
  {"Watchdog Timer",     0x2102B0000ULL, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"AICD",               0x20e100000ULL, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UART",               0x20A0C0000ULL, 0x00004000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
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
