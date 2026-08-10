/** @file
*  File managing the MMU for ARMv8 architecture
*
*  Copyright (c) 2011-2025, ARM Limited. All rights reserved.
*  Copyright (c) 2016, Linaro Limited. All rights reserved.
*  Copyright (c) 2017, Intel Corporation. All rights reserved.<BR>
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <AArch64/AArch64.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include "ArmMmuLibInternal.h"

STATIC  ARM_REPLACE_LIVE_TRANSLATION_ENTRY  mReplaceLiveEntryFunc = ArmReplaceLiveTranslationEntry;

/**
  Whether the current translation regime is either EL1&0 or EL2&0, and
  therefore supports non-global, ASID-scoped memory mappings.
 **/
STATIC
BOOLEAN
TranslationRegimeIsDual (
  VOID
  )
{
  if (ArmReadCurrentEL () == AARCH64_EL2) {
    return (ArmReadHcr () & ARM_HCR_E2H) != 0;
  }

  return TRUE;
}

STATIC
UINT64
ArmMemoryAttributeToPageAttribute (
  IN ARM_MEMORY_REGION_ATTRIBUTES  Attributes
  )
{
  UINT64  Permissions;

  switch (Attributes) {
    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO:
      Permissions = TT_AP_NO_RO;
      break;

    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_XP:
    case ARM_MEMORY_REGION_ATTRIBUTE_DEVICE:
      if (!TranslationRegimeIsDual ()) {
        Permissions = TT_XN_MASK;
      } else {
        Permissions = TT_UXN_MASK | TT_PXN_MASK;
      }

      break;
    default:
      Permissions = 0;
      break;
  }

  switch (Attributes) {
    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_NONSHAREABLE:
      return TT_ATTR_INDX_MEMORY_WRITE_BACK | Permissions;

    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK:
    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_RO:
    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_XP:
      return TT_ATTR_INDX_MEMORY_WRITE_BACK | TT_SH_INNER_SHAREABLE | Permissions;

    case ARM_MEMORY_REGION_ATTRIBUTE_WRITE_THROUGH:
      return TT_ATTR_INDX_MEMORY_WRITE_THROUGH | TT_SH_INNER_SHAREABLE;

    // Uncached and device mappings are treated as outer shareable by default,
    case ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED:
      return TT_ATTR_INDX_MEMORY_NON_CACHEABLE | Permissions;

    default:
      ASSERT (0);
    case ARM_MEMORY_REGION_ATTRIBUTE_DEVICE:
      return TT_ATTR_INDX_DEVICE_MEMORY | Permissions;
  }
}

#define MAX_VA_BITS  52

//
// AArch64Mmu.h only defines the 4 KB translation granule. Apple A-series cores
// (e.g. A9/S8003) do not implement the 4 KB granule at all (ID_AA64MMFR0_EL1.
// TGran4 == 0b1111) and require the 16 KB granule, so define the extra TCR.TGx
// encodings we need here. The 4 KB variants come from AArch64Mmu.h.
//   TCR.TG0: 0b00 = 4 KB, 0b01 = 64 KB, 0b10 = 16 KB
//   TCR.TG1: 0b01 = 16 KB, 0b10 = 4 KB, 0b11 = 64 KB
//
#define TCR_TG0_16KB  (2UL << 14)
#define TCR_TG1_16KB  (1UL << 30)
#define TCR_TG0_64KB  (1UL << 14)
#define TCR_TG1_64KB  (3UL << 30)

// Not exposed through a public ArmLib header, but provided by ArmLib.
UINTN
EFIAPI
ArmReadIdAA64Mmfr0 (
  VOID
  );

//
// Translation granule parameters, selected at runtime from the granule the CPU
// actually implements (see MmuSelectGranule). Defaults describe the 4 KB
// granule so that, until selection runs, behaviour matches the stock library.
//
// T0SZ can be below mMinT0Sz when LPA2 is in use, meaning the page table starts
// at level -1.
//
STATIC UINTN   mBitsPerLevel  = 9;                    // translation bits per table level
STATIC UINTN   mMinT0Sz       = 16;                   // T0SZ of a full-depth walk
STATIC UINTN   mEntryCount    = TT_ENTRY_COUNT;       // descriptors per table (granule / 8)
STATIC UINTN   mGranuleSize   = SIZE_4KB;             // page/table size in bytes
STATIC UINTN   mPagesPerTable = SIZE_4KB / SIZE_4KB;  // EFI 4 KB pages per granule-sized table
STATIC UINT64  mTcrTg0        = TCR_TG0_4KB;
STATIC UINT64  mTcrTg1        = TCR_TG1_4KB;

/**
  Select the translation granule based on what the CPU implements, reading
  ID_AA64MMFR0_EL1. Prefers 4 KB (the EDK2 default), falls back to 16 KB then
  64 KB. Idempotent, and cheap enough to call from every entry point so that
  each linked module (SEC, DXE, ...) configures itself correctly regardless of
  constructor ordering.
**/
STATIC
VOID
MmuSelectGranule (
  VOID
  )
{
  UINT64  Mmfr0;
  UINT32  TGran4;
  UINT32  TGran16;

  Mmfr0   = ArmReadIdAA64Mmfr0 ();
  TGran4  = (UINT32)((Mmfr0 >> 28) & 0xF); // 0b0000/0b0001 => 4 KB; 0b1111 => unsupported
  TGran16 = (UINT32)((Mmfr0 >> 20) & 0xF); // 0b0000 => 16 KB unsupported; nonzero => supported

  if (TGran4 != 0xF) {
    // 4 KB granule: keep the defaults set above.
    return;
  }

  if (TGran16 != 0) {
    // 16 KB granule (Apple A-series). 2048 entries per 16 KB table, 11 bits
    // resolved per level; mMinT0Sz keeps GetRootTableLevel()/BlockShift math
    // consistent: 4 * 11 + 6 == 64 - 14 (the level-3 page shift).
    mBitsPerLevel  = 11;
    mMinT0Sz       = 6;
    mEntryCount    = SIZE_16KB / sizeof (UINT64);
    mGranuleSize   = SIZE_16KB;
    mPagesPerTable = SIZE_16KB / SIZE_4KB;
    mTcrTg0        = TCR_TG0_16KB;
    mTcrTg1        = TCR_TG1_16KB;
    return;
  }

  // 64 KB-only cores are not a current target; fail loudly rather than
  // silently program an unsupported 4 KB granule.
  ASSERT (FALSE);
}

STATIC
VOID
SetOutputAddress (
  IN  UINTN    *Entry,
  IN  UINTN    Address,
  IN  BOOLEAN  Lpa2Enabled
  )
{
  if (Lpa2Enabled) {
    *Entry &= ~(TT_ADDRESS_MASK_BLOCK_ENTRY_LPA2 | TT_UPPER_ADDRESS_MASK);
    *Entry |= ((UINTN)Address & TT_ADDRESS_MASK_BLOCK_ENTRY_LPA2) | (((UINTN)Address >> 50) << 8);
  } else {
    *Entry &= ~TT_ADDRESS_MASK_BLOCK_ENTRY;
    *Entry |= (Address & TT_ADDRESS_MASK_BLOCK_ENTRY);
  }
}

STATIC
UINT64
GetOutputAddress (
  IN  UINT64   Entry,
  IN  BOOLEAN  Lpa2Enabled
  )
{
  if (Lpa2Enabled) {
    return (Entry & TT_ADDRESS_MASK_BLOCK_ENTRY_LPA2) | ((Entry & TT_UPPER_ADDRESS_MASK) << (50 - 8));
  } else {
    return Entry & TT_ADDRESS_MASK_BLOCK_ENTRY;
  }
}

STATIC
UINTN
GetRootTableEntryCount (
  IN  UINTN  T0SZ
  )
{
  return mEntryCount >> (T0SZ - mMinT0Sz) % mBitsPerLevel;
}

STATIC
INTN
GetRootTableLevel (
  IN  UINTN  T0SZ
  )
{
  INTN  RootTableLevel;

  RootTableLevel =  (T0SZ < mMinT0Sz) ? -1 : (INTN)(T0SZ - mMinT0Sz) / mBitsPerLevel;
  ASSERT (RootTableLevel >= 0 || ArmLpa2Enabled ());

  return RootTableLevel;
}

STATIC
VOID
ReplaceTableEntry (
  IN  UINT64   *Entry,
  IN  UINT64   Value,
  IN  UINT64   RegionStart,
  IN  UINT64   BlockMask,
  IN  BOOLEAN  IsLiveBlockMapping
  )
{
  BOOLEAN  DisableMmu;

  //
  // Replacing a live block entry with a table entry (or vice versa) requires a
  // break-before-make sequence as per the architecture. This means the mapping
  // must be made invalid and cleaned from the TLBs first, and this is a bit of
  // a hassle if the mapping in question covers the code that is actually doing
  // the mapping and the unmapping, and so we only bother with this if actually
  // necessary.
  //

  if (!IsLiveBlockMapping || !ArmMmuEnabled ()) {
    // If the mapping is not a live block mapping, or the MMU is not on yet, we
    // can simply overwrite the entry.
    *Entry = Value;
    ArmUpdateTranslationTableEntry (Entry, (VOID *)(UINTN)RegionStart);
  } else {
    // If the mapping in question does not cover the code that updates the
    // entry in memory, or the entry that we are intending to update, we can
    // use an ordinary break before make. Otherwise, we will need to
    // temporarily disable the MMU.
    DisableMmu = FALSE;
    if ((((RegionStart ^ (UINTN)mReplaceLiveEntryFunc) & ~BlockMask) == 0) ||
        (((RegionStart ^ (UINTN)Entry) & ~BlockMask) == 0))
    {
      DisableMmu = TRUE;
      DEBUG ((DEBUG_WARN, "%a: splitting block entry with MMU disabled\n", __func__));
    }

    mReplaceLiveEntryFunc (Entry, Value, RegionStart, DisableMmu);
  }
}

STATIC
VOID
FreePageTablesRecursive (
  IN  UINT64   *TranslationTable,
  IN  UINTN    Level,
  IN  BOOLEAN  Lpa2Enabled
  )
{
  UINTN  Index;

  ASSERT (Level <= 3);

  if (Level < 3) {
    for (Index = 0; Index < mEntryCount; Index++) {
      if ((TranslationTable[Index] & TT_TYPE_MASK) == TT_TYPE_TABLE_ENTRY) {
        FreePageTablesRecursive (
          (VOID *)GetOutputAddress (
                    TranslationTable[Index],
                    Lpa2Enabled
                    ),
          Level + 1,
          Lpa2Enabled
          );
      }
    }
  }

  FreePages (TranslationTable, mPagesPerTable);
}

STATIC
BOOLEAN
IsBlockEntry (
  IN  UINT64  Entry,
  IN  UINTN   Level
  )
{
  if (Level == 3) {
    return (Entry & TT_TYPE_MASK) == TT_TYPE_BLOCK_ENTRY_LEVEL3;
  }

  return (Entry & TT_TYPE_MASK) == TT_TYPE_BLOCK_ENTRY;
}

STATIC
BOOLEAN
IsTableEntry (
  IN  UINT64  Entry,
  IN  UINTN   Level
  )
{
  if (Level == 3) {
    //
    // TT_TYPE_TABLE_ENTRY aliases TT_TYPE_BLOCK_ENTRY_LEVEL3
    // so we need to take the level into account as well.
    //
    return FALSE;
  }

  return (Entry & TT_TYPE_MASK) == TT_TYPE_TABLE_ENTRY;
}

STATIC
EFI_STATUS
UpdateRegionMappingRecursive (
  IN  UINT64   RegionStart,
  IN  UINT64   RegionEnd,
  IN  UINT64   AttributeSetMask,
  IN  UINT64   AttributeClearMask,
  IN  UINT64   *PageTable,
  IN  INTN     Level,
  IN  BOOLEAN  IsRootTable,
  IN  BOOLEAN  TableIsLive,
  IN  BOOLEAN  Lpa2Enabled
  )
{
  UINTN       BlockShift;
  UINT64      BlockMask;
  UINT64      BlockEnd;
  UINT64      *Entry;
  UINT64      EntryValue;
  VOID        *TranslationTable;
  EFI_STATUS  Status;
  BOOLEAN     NextTableIsLive;
  VOID        *TablesToFree[2];

  ASSERT (((RegionStart | RegionEnd) & EFI_PAGE_MASK) == 0);

  BlockShift = (Level + 1) * mBitsPerLevel + mMinT0Sz;
  BlockMask  = MAX_UINT64 >> BlockShift;

  TablesToFree[0] = NULL;
  TablesToFree[1] = NULL;

  DEBUG ((
    DEBUG_VERBOSE,
    "%a(%d): %llx - %llx set %lx clr %lx\n",
    __func__,
    Level,
    RegionStart,
    RegionEnd,
    AttributeSetMask,
    AttributeClearMask
    ));

  for ( ; RegionStart < RegionEnd; RegionStart = BlockEnd) {
    BlockEnd = MIN (RegionEnd, (RegionStart | BlockMask) + 1);
    Entry    = &PageTable[(RegionStart >> (64 - BlockShift)) & (mEntryCount - 1)];

    //
    // If RegionStart or BlockEnd is not aligned to the block size at this
    // level, we will have to create a table mapping in order to map less
    // than a block, and recurse to create the block or page entries at
    // the next level. No block mappings are allowed at all at level 0,
    // so in that case, we have to recurse unconditionally.
    //
    // One special case to take into account is any region that covers the page
    // table itself: if we'd cover such a region with block mappings, we are
    // more likely to end up in the situation later where we need to disable
    // the MMU in order to update page table entries safely, so prefer page
    // mappings in that particular case.
    //
    if ((Level <= 0) || (((RegionStart | BlockEnd) & BlockMask) != 0) ||
        ((Level < 3) && (((UINT64)PageTable & ~BlockMask) == RegionStart)) ||
        IsTableEntry (*Entry, Level))
    {
      ASSERT (Level < 3);

      if (!IsTableEntry (*Entry, Level)) {
        //
        // If the region we are trying to map is already covered by a block
        // entry with the right attributes, don't bother splitting it up.
        //
        if (IsBlockEntry (*Entry, Level) &&
            ((*Entry & TT_ATTRIBUTES_MASK & ~AttributeClearMask) == AttributeSetMask))
        {
          continue;
        }

        //
        // No table entry exists yet, so we need to allocate a page table
        // for the next level. Tables must be granule-sized and granule-
        // aligned, which is larger than one EFI page for the 16 KB/64 KB
        // granules, so allocate with explicit alignment.
        //
        TranslationTable = AllocateAlignedPages (mPagesPerTable, mGranuleSize);
        if (TranslationTable == NULL) {
          return EFI_OUT_OF_RESOURCES;
        }

        //
        // Allocating a page may have split this block if a guard page
        // was allocated in this block. Check if this is already split
        // and if so skip the splitting logic
        //
        if (IsTableEntry (*Entry, Level)) {
          //
          // Don't free the page table here, we may end up recreating the
          // large page. This mapping may extend across the block boundary,
          // so its possible we could have two pages to free in the worst case.
          //
          if (TablesToFree[0] == NULL) {
            TablesToFree[0] = TranslationTable;
          } else {
            TablesToFree[1] = TranslationTable;
          }

          TranslationTable = (VOID *)GetOutputAddress (*Entry, Lpa2Enabled);
          NextTableIsLive  = TableIsLive;
        } else {
          if (!ArmMmuEnabled ()) {
            //
            // Make sure we are not inadvertently hitting in the caches
            // when populating the page tables.
            //
            InvalidateDataCacheRange (TranslationTable, mGranuleSize);
          }

          ZeroMem (TranslationTable, mGranuleSize);

          if (IsBlockEntry (*Entry, Level)) {
            //
            // We are splitting an existing block entry, so we have to populate
            // the new table with the attributes of the block entry it replaces.
            //
            Status = UpdateRegionMappingRecursive (
                       RegionStart & ~BlockMask,
                       (RegionStart | BlockMask) + 1,
                       *Entry & TT_ATTRIBUTES_MASK,
                       0,
                       TranslationTable,
                       Level + 1,
                       FALSE,
                       FALSE,
                       Lpa2Enabled
                       );
            if (EFI_ERROR (Status)) {
              //
              // The range we passed to UpdateRegionMappingRecursive () is block
              // aligned, so it is guaranteed that no further pages were allocated
              // by it, and so we only have to free the page we allocated here.
              //
              FreePages (TranslationTable, mPagesPerTable);
              return Status;
            }
          }

          NextTableIsLive = FALSE;
        }
      } else {
        TranslationTable = (VOID *)GetOutputAddress (*Entry, Lpa2Enabled);
        NextTableIsLive  = TableIsLive;
      }

      //
      // Recurse to the next level
      //
      Status = UpdateRegionMappingRecursive (
                 RegionStart,
                 BlockEnd,
                 AttributeSetMask,
                 AttributeClearMask,
                 TranslationTable,
                 Level + 1,
                 FALSE,
                 NextTableIsLive,
                 Lpa2Enabled
                 );
      if (EFI_ERROR (Status)) {
        if (!IsTableEntry (*Entry, Level)) {
          //
          // We are creating a new table entry, so on failure, we can free all
          // allocations we made recursively, given that the whole subhierarchy
          // has not been wired into the live page tables yet. (This is not
          // possible for existing table entries, since we cannot revert the
          // modifications we made to the subhierarchy it represents.)
          //
          FreePageTablesRecursive (TranslationTable, Level + 1, Lpa2Enabled);
        }

        return Status;
      }

      if (!IsTableEntry (*Entry, Level)) {
        EntryValue = TT_TYPE_TABLE_ENTRY;
        SetOutputAddress (&EntryValue, (UINTN)TranslationTable, Lpa2Enabled);

        ReplaceTableEntry (
          Entry,
          EntryValue,
          RegionStart,
          BlockMask,
          TableIsLive && IsBlockEntry (*Entry, Level)
          );
      }
    } else {
      EntryValue = (*Entry & AttributeClearMask) | AttributeSetMask;
      // Below clears shareability bits when LPA2 is in use
      SetOutputAddress (&EntryValue, RegionStart, Lpa2Enabled);
      EntryValue |= (Level == 3) ? TT_TYPE_BLOCK_ENTRY_LEVEL3
                                 : TT_TYPE_BLOCK_ENTRY;

      ReplaceTableEntry (Entry, EntryValue, RegionStart, BlockMask, FALSE);
    }
  }

  //
  // We may have left up to two orphaned page table pages if we discovered a
  // recursive call already split a block on either side of a misaligned region.
  //
  if (TablesToFree[0] != NULL) {
    FreePages (TablesToFree[0], mPagesPerTable);
    TablesToFree[0] = NULL;
  }

  if (TablesToFree[1] != NULL) {
    FreePages (TablesToFree[1], mPagesPerTable);
    TablesToFree[1] = NULL;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
UpdateRegionMapping (
  IN  UINT64   RegionStart,
  IN  UINT64   RegionLength,
  IN  UINT64   AttributeSetMask,
  IN  UINT64   AttributeClearMask,
  IN  UINT64   *RootTable,
  IN  BOOLEAN  TableIsLive,
  IN  BOOLEAN  Lpa2Enabled
  )
{
  UINTN  T0SZ;

  if (((RegionStart | RegionLength) & EFI_PAGE_MASK) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "%a RegionStart: 0x%llx or RegionLength: 0x%llx are not page aligned!\n",
      __func__,
      RegionStart,
      RegionLength
      ));
    return EFI_INVALID_PARAMETER;
  }

  T0SZ = ArmGetTCR () & TCR_T0SZ_MASK;

  return UpdateRegionMappingRecursive (
           RegionStart,
           RegionStart + RegionLength,
           AttributeSetMask,
           AttributeClearMask,
           RootTable,
           GetRootTableLevel (T0SZ),
           TRUE,
           TableIsLive,
           Lpa2Enabled
           );
}

STATIC
EFI_STATUS
FillTranslationTable (
  IN  UINT64                        *RootTable,
  IN  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryRegion,
  IN  BOOLEAN                       Lpa2Enabled
  )
{
  return UpdateRegionMapping (
           MemoryRegion->VirtualBase,
           MemoryRegion->Length,
           ArmMemoryAttributeToPageAttribute (MemoryRegion->Attributes) | TT_AF,
           0,
           RootTable,
           FALSE,
           Lpa2Enabled
           );
}

STATIC
UINT64
GcdAttributeToPageAttribute (
  IN UINT64  GcdAttributes
  )
{
  UINT64  PageAttributes;

  switch (GcdAttributes & EFI_MEMORY_CACHETYPE_MASK) {
    case EFI_MEMORY_UC:
      PageAttributes = TT_ATTR_INDX_DEVICE_MEMORY;
      break;
    case EFI_MEMORY_WC:
      PageAttributes = TT_ATTR_INDX_MEMORY_NON_CACHEABLE;
      break;
    case EFI_MEMORY_WT:
      PageAttributes = TT_ATTR_INDX_MEMORY_WRITE_THROUGH | TT_SH_INNER_SHAREABLE;
      break;
    case EFI_MEMORY_WB:
      PageAttributes = TT_ATTR_INDX_MEMORY_WRITE_BACK | TT_SH_INNER_SHAREABLE;
      break;
    default:
      PageAttributes = TT_ATTR_INDX_MASK;
      break;
  }

  if (((GcdAttributes & EFI_MEMORY_XP) != 0) ||
      ((GcdAttributes & EFI_MEMORY_CACHETYPE_MASK) == EFI_MEMORY_UC))
  {
    if (!TranslationRegimeIsDual ()) {
      PageAttributes |= TT_XN_MASK;
    } else {
      PageAttributes |= TT_UXN_MASK | TT_PXN_MASK;
    }
  }

  if ((GcdAttributes & EFI_MEMORY_RO) != 0) {
    PageAttributes |= TT_AP_NO_RO;
  }

  if ((GcdAttributes & EFI_MEMORY_RP) == 0) {
    PageAttributes |= TT_AF;
  }

  return PageAttributes;
}

/**
  Set the requested memory permission attributes on a region of memory.

  BaseAddress and Length must be aligned to EFI_PAGE_SIZE.

  If Attributes contains a memory type attribute (EFI_MEMORY_UC/WC/WT/WB), the
  region is mapped according to this memory type, and additional memory
  permission attributes (EFI_MEMORY_RP/RO/XP) are taken into account as well,
  discarding any permission attributes that are currently set for the region.
  AttributeMask is ignored in this case, and must be set to 0x0.

  If Attributes contains only a combination of memory permission attributes
  (EFI_MEMORY_RP/RO/XP), each page in the region will retain its existing
  memory type, even if it is not uniformly set across the region. In this case,
  AttributesMask may be set to a mask of permission attributes, and memory
  permissions omitted from this mask will not be updated for any page in the
  region. All attributes appearing in Attributes must appear in AttributeMask
  as well. (Attributes & ~AttributeMask must produce 0x0)

  @param[in]  BaseAddress     The physical address that is the start address of
                              a memory region.
  @param[in]  Length          The size in bytes of the memory region.
  @param[in]  Attributes      Mask of memory attributes to set.
  @param[in]  AttributeMask   Mask of memory attributes to take into account.

  @retval EFI_SUCCESS           The attributes were set for the memory region.
  @retval EFI_INVALID_PARAMETER BaseAddress or Length is not suitably aligned.
                                Invalid combination of Attributes and
                                AttributeMask.
  @retval EFI_OUT_OF_RESOURCES  Requested attributes cannot be applied due to
                                lack of system resources.

**/
EFI_STATUS
ArmSetMemoryAttributes (
  IN EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN UINT64                Length,
  IN UINT64                Attributes,
  IN UINT64                AttributeMask
  )
{
  UINT64  PageAttributes;
  UINT64  PageAttributeMask;

  // Ensure the granule globals match the hardware for this module (e.g. CpuDxe
  // links its own copy of this library and may call in before ArmConfigureMmu).
  MmuSelectGranule ();

  PageAttributes    = GcdAttributeToPageAttribute (Attributes);
  PageAttributeMask = 0;

  if ((Attributes & EFI_MEMORY_CACHETYPE_MASK) == 0) {
    //
    // No memory type was set in Attributes, so we are going to update the
    // permissions only.
    //
    PageAttributes   &= TT_AP_MASK | TT_UXN_MASK | TT_PXN_MASK | TT_AF;
    PageAttributeMask = ~(TT_ADDRESS_MASK_BLOCK_ENTRY | TT_AP_MASK |
                          TT_PXN_MASK | TT_XN_MASK | TT_AF);
    if (AttributeMask != 0) {
      if (((AttributeMask & ~(UINT64)(EFI_MEMORY_RP|EFI_MEMORY_RO|EFI_MEMORY_XP)) != 0) ||
          ((Attributes & ~AttributeMask) != 0))
      {
        return EFI_INVALID_PARAMETER;
      }

      // Add attributes omitted from AttributeMask to the set of attributes to preserve
      PageAttributeMask |= GcdAttributeToPageAttribute (~AttributeMask) &
                           (TT_AP_MASK | TT_UXN_MASK | TT_PXN_MASK | TT_AF);
    }
  } else {
    ASSERT (AttributeMask == 0);
    if (AttributeMask != 0) {
      return EFI_INVALID_PARAMETER;
    }
  }

  return UpdateRegionMapping (
           BaseAddress,
           Length,
           PageAttributes,
           PageAttributeMask,
           ArmGetTTBR0BaseAddress (),
           TRUE,
           ArmLpa2Enabled ()
           );
}

EFI_STATUS
EFIAPI
ArmConfigureMmu (
  IN  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable,
  OUT VOID                          **TranslationTableBase OPTIONAL,
  OUT UINTN                         *TranslationTableSize OPTIONAL
  )
{
  VOID        *TranslationTable;
  UINTN       MaxAddressBits;
  UINT64      MaxAddress;
  UINTN       T0SZ;
  UINTN       RootTableEntryCount;
  UINT64      TCR;
  EFI_STATUS  Status;

  ASSERT (ArmReadCurrentEL () < AARCH64_EL3);
  if (ArmReadCurrentEL () == AARCH64_EL3) {
    return EFI_UNSUPPORTED;
  }

  if (MemoryTable == NULL) {
    ASSERT (MemoryTable != NULL);
    return EFI_INVALID_PARAMETER;
  }

  // Select the translation granule the CPU actually implements (Apple A-series
  // require 16 KB) before computing any table geometry below.
  MmuSelectGranule ();

  //
  // Limit the virtual address space to what we can actually use: UEFI
  // mandates a 1:1 mapping, so no point in making the virtual address
  // space larger than the physical address space. We also have to take
  // into account the architectural limitations that result from UEFI's
  // use of 4 KB pages.
  //
  MaxAddressBits = MIN (ArmGetPhysicalAddressBits (), MAX_VA_BITS);
  MaxAddress     = LShiftU64 (1ULL, MaxAddressBits) - 1;

  T0SZ                = 64 - MaxAddressBits;
  RootTableEntryCount = GetRootTableEntryCount (T0SZ);

  //
  // Set TCR that allows us to retrieve T0SZ in the subsequent functions
  //
  if (!TranslationRegimeIsDual ()) {
    // Note: Bits 23 and 31 are reserved(RES1) bits in TCR_EL2
    TCR = T0SZ | (1UL << 31) | (1UL << 23) | mTcrTg0;

    // Set the Physical Address Size using MaxAddress
    if (MaxAddress < SIZE_4GB) {
      TCR |= TCR_PS_4GB;
    } else if (MaxAddress < SIZE_64GB) {
      TCR |= TCR_PS_64GB;
    } else if (MaxAddress < SIZE_1TB) {
      TCR |= TCR_PS_1TB;
    } else if (MaxAddress < SIZE_4TB) {
      TCR |= TCR_PS_4TB;
    } else if (MaxAddress < SIZE_16TB) {
      TCR |= TCR_PS_16TB;
    } else if (MaxAddress < SIZE_256TB) {
      TCR |= TCR_PS_256TB;
    } else if ((MaxAddress < SIZE_4PB) && ArmHas52BitTgran4 ()) {
      TCR |= TCR_PS_4PB | TCR_DS_NVHE;
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "ArmConfigureMmu: The MaxAddress 0x%lX is not supported by this MMU configuration.\n",
        MaxAddress
        ));
      ASSERT (0); // Bigger than 48/52-bit memory space are not supported
      return EFI_UNSUPPORTED;
    }
  } else {
    // Due to Cortex-A57 erratum #822227 we must set TG1[1] == 1, regardless of EPD1.
    TCR = T0SZ | mTcrTg0 | mTcrTg1 | TCR_EPD1;

    // Set the Physical Address Size using MaxAddress
    if (MaxAddress < SIZE_4GB) {
      TCR |= TCR_IPS_4GB;
    } else if (MaxAddress < SIZE_64GB) {
      TCR |= TCR_IPS_64GB;
    } else if (MaxAddress < SIZE_1TB) {
      TCR |= TCR_IPS_1TB;
    } else if (MaxAddress < SIZE_4TB) {
      TCR |= TCR_IPS_4TB;
    } else if (MaxAddress < SIZE_16TB) {
      TCR |= TCR_IPS_16TB;
    } else if (MaxAddress < SIZE_256TB) {
      TCR |= TCR_IPS_256TB;
    } else if ((MaxAddress < SIZE_4PB) && ArmHas52BitTgran4 ()) {
      TCR |= TCR_IPS_4PB | TCR_DS;
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "ArmConfigureMmu: The MaxAddress 0x%lX is not supported by this MMU configuration.\n",
        MaxAddress
        ));
      ASSERT (0); // Bigger than 48/52-bit memory space are not supported
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Translation table walks are always cache coherent on ARMv8-A, so cache
  // maintenance on page tables is never needed. Since there is a risk of
  // loss of coherency when using mismatched attributes, and given that memory
  // is mapped cacheable except for extraordinary cases (such as non-coherent
  // DMA), have the page table walker perform cached accesses as well, and
  // assert below that matches the attributes we use for CPU accesses to
  // the region.
  //
  TCR |= TCR_SH_INNER_SHAREABLE |
         TCR_RGN_OUTER_WRITE_BACK_ALLOC |
         TCR_RGN_INNER_WRITE_BACK_ALLOC;

  // Set TCR
  ArmSetTCR (TCR);

  // Allocate pages for translation table. The root table must be granule-sized
  // and granule-aligned (16 KB on Apple A-series), which exceeds a single EFI
  // page, so allocate with explicit alignment.
  TranslationTable = AllocateAlignedPages (mPagesPerTable, mGranuleSize);
  if (TranslationTable == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (TranslationTableBase != NULL) {
    *TranslationTableBase = TranslationTable;
  }

  if (TranslationTableSize != NULL) {
    *TranslationTableSize = RootTableEntryCount * sizeof (UINT64);
  }

  if (!ArmMmuEnabled ()) {
    //
    // Make sure we are not inadvertently hitting in the caches
    // when populating the page tables.
    //
    InvalidateDataCacheRange (
      TranslationTable,
      RootTableEntryCount * sizeof (UINT64)
      );
  }

  ZeroMem (TranslationTable, RootTableEntryCount * sizeof (UINT64));

  while (MemoryTable->Length != 0) {
    Status = FillTranslationTable (TranslationTable, MemoryTable, ArmLpa2Enabled ());
    if (EFI_ERROR (Status)) {
      goto FreeTranslationTable;
    }

    MemoryTable++;
  }

  //
  // EFI_MEMORY_UC ==> MAIR_ATTR_DEVICE_MEMORY
  // EFI_MEMORY_WC ==> MAIR_ATTR_NORMAL_MEMORY_NON_CACHEABLE
  // EFI_MEMORY_WT ==> MAIR_ATTR_NORMAL_MEMORY_WRITE_THROUGH
  // EFI_MEMORY_WB ==> MAIR_ATTR_NORMAL_MEMORY_WRITE_BACK
  //
  ArmSetMAIR (
    MAIR_ATTR (TT_ATTR_INDX_DEVICE_MEMORY, MAIR_ATTR_DEVICE_MEMORY)               |
    MAIR_ATTR (TT_ATTR_INDX_MEMORY_NON_CACHEABLE, MAIR_ATTR_NORMAL_MEMORY_NON_CACHEABLE) |
    MAIR_ATTR (TT_ATTR_INDX_MEMORY_WRITE_THROUGH, MAIR_ATTR_NORMAL_MEMORY_WRITE_THROUGH) |
    MAIR_ATTR (TT_ATTR_INDX_MEMORY_WRITE_BACK, MAIR_ATTR_NORMAL_MEMORY_WRITE_BACK)
    );

  if ((TCR & TCR_IPS_MASK) == TCR_IPS_4PB) {
    ArmSetTTBR0 (
      (VOID *)
      (((UINTN)TranslationTable & 0xffffffffffc0) |
       (((UINTN)TranslationTable >> 48) << 2))
      );
  } else {
    ArmSetTTBR0 (TranslationTable);
  }

  if (!ArmMmuEnabled ()) {
    ArmDisableAlignmentCheck ();
    ArmEnableStackAlignmentCheck ();
    ArmEnableInstructionCache ();
    ArmEnableDataCache ();

    ArmEnableMmu ();
  }

  return EFI_SUCCESS;

FreeTranslationTable:
  FreePages (TranslationTable, mPagesPerTable);
  return Status;
}

/**
  Check whether a 52-bit output address can be described
  by the translation tables (FEAT_LPA2).
  @retval  TRUE    52-bit output address is enabled (LPA2 enabled).
  @retval  FALSE   52-bit output address is disabled (LPA2 disabled).

**/
BOOLEAN
ArmLpa2Enabled (
  VOID
  )
{
  UINT64  TCR;

  TCR = ArmGetTCR ();

  return !TranslationRegimeIsDual () ?
         ((TCR & TCR_DS_NVHE) != 0) :
         ((TCR & TCR_DS) != 0);
}

RETURN_STATUS
EFIAPI
ArmMmuBaseLibConstructor (
  VOID
  )
{
  extern UINT32  ArmReplaceLiveTranslationEntrySize;
  VOID           *Hob;

  // Configure the granule globals for this module up front.
  MmuSelectGranule ();

  Hob = GetFirstGuidHob (&gArmMmuReplaceLiveTranslationEntryFuncGuid);
  if (Hob != NULL) {
    mReplaceLiveEntryFunc = *(ARM_REPLACE_LIVE_TRANSLATION_ENTRY *)GET_GUID_HOB_DATA (Hob);
  } else {
    //
    // The ArmReplaceLiveTranslationEntry () helper function may be invoked
    // with the MMU off so we have to ensure that it gets cleaned to the PoC
    //
    WriteBackDataCacheRange (
      (VOID *)(UINTN)ArmReplaceLiveTranslationEntry,
      ArmReplaceLiveTranslationEntrySize
      );
  }

  return RETURN_SUCCESS;
}
