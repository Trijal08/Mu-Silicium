/**
  Based on https://github.com/SamuelTulach/rainbow

  Copyright (c) 2021 Samuel Tulach
  Copyright (c) 2022-2023 DuoWoA authors

  SPDX-License-Identifier: MIT
**/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/ErrataPatchesLib.h>
#include <Library/PerformanceLib.h>

#include <IndustryStandard/PeImage.h>

#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

#include "KernelErrataPatcherLib.h"

//
// Early Patch Limit
//
// How many distinct Images the Early Hook is willing to look at before it gives up
// and leaves the Rest to ExitBootServices. Only the Boot Manager and the OS Loader
// are expected, so this is Headroom rather than a Target.
//
#define MAX_EARLY_PATCH_IMAGES 4

//
// Diagnostic Trace Depth
//
// How many Stack Words to read back through looking for Callers, and how many of them
// to report. The Loader's own Allocation Wrapper keeps a large Local Frame, so its
// saved Return Address sits well above the Hook's own Frame and a short Window finds
// nothing but the Wrapper itself.
//
#define DIAG_STACK_WORDS 512
#define DIAG_FRAMES      12

//
// How deep to follow the Frame Pointer Chain.
//
// Twelve was not enough: the Chain ran out at BlMmAllocatePagesInRange, one Frame short
// of naming who asked for the Memory. Debug Output is capped at two hundred and fifty
// six Characters, so this is reported across two Lines rather than one long one.
//
#define DIAG_CHAIN_FRAMES 20
#define DIAG_CHAIN_SPLIT  10

//
// How many Frame Records to follow before giving up, in case the Chain is not intact.
//
#define DIAG_MAX_CHAIN 64

//
// Global Variables
//
STATIC EFI_EXIT_BOOT_SERVICES mOriginalEfiExitBootServices;

//
// Early Patch State
//
// Some Errata have to be Corrected before the OS Loader reaches ExitBootServices.
// Windows 11 22H2 and later probe for Qualcomm Secure Launch from OslPrepareTarget,
// which runs long before the Loader hands the Machine over, and the Fault that
// follows on Exynos ends in a Handler whose whole Body is a Branch to itself. Patching
// at ExitBootServices would never be reached.
//
// The Boot Manager loads the OS Loader with its own PE Loader rather than through
// LoadImage, so no Firmware Event announces it. Allocation, however, goes through
// Boot Services either way, so AllocatePages is hooked and the Return Address tells
// which Image is currently running.
//
STATIC EFI_ALLOCATE_PAGES   mOriginalAllocatePages = NULL;
STATIC EFI_PHYSICAL_ADDRESS mPatchedImages[MAX_EARLY_PATCH_IMAGES];
STATIC UINTN                mPatchedImageCount = 0;

//
// Last Image seen allocating, kept so the common Case costs two Comparisons instead of
// a Walk back through the whole Image looking for its Header.
//
STATIC EFI_PHYSICAL_ADDRESS mLastImageBase    = 0;
STATIC EFI_PHYSICAL_ADDRESS mLastImageEnd     = 0;
STATIC UINT64               mLastImageCodeEnd = 0;
STATIC UINTN                mAllocationCount  = 0;

/**
  Find where an Image's executable Sections stop.

  Return Addresses can only point at Code, but plenty of Pointers into an Image's Data
  sit on the Stack as well and look identical to a Search that only checks whether a
  Value falls inside the Image. Knowing where the Code ends is what separates them.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.

  @return The highest Offset covered by an executable Section, or Length if the Headers
          cannot be read, which simply leaves the Filter as wide as it was before.
**/
STATIC
UINT64
ImageCodeEnd (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_IMAGE_DOS_HEADER     *DosHeader = (EFI_IMAGE_DOS_HEADER *)(UINTN)Base;
  EFI_IMAGE_NT_HEADERS64   *NtHeader;
  EFI_IMAGE_SECTION_HEADER *Section;
  UINT64                    CodeEnd = 0;

  if (DosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE || DosHeader->e_lfanew >= Length) {
    return Length;
  }

  NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(UINTN)(Base + DosHeader->e_lfanew);
  if (NtHeader->Signature != EFI_IMAGE_NT_SIGNATURE) {
    return Length;
  }

  Section = (EFI_IMAGE_SECTION_HEADER *)(UINTN)((UINTN)NtHeader + sizeof (UINT32) +
                                                sizeof (EFI_IMAGE_FILE_HEADER) +
                                                NtHeader->FileHeader.SizeOfOptionalHeader);

  for (UINT16 i = 0; i < NtHeader->FileHeader.NumberOfSections; i++, Section++) {
    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0) {
      continue;
    }

    if (Section->VirtualAddress + Section->Misc.VirtualSize > CodeEnd) {
      CodeEnd = Section->VirtualAddress + Section->Misc.VirtualSize;
    }
  }

  return (CodeEnd != 0) ? CodeEnd : Length;
}

/**
  Remove the Early Allocation Hook and restore Boot Services.
**/
STATIC
VOID
RemoveEarlyPatchHook (VOID)
{
  gBS->AllocatePages = mOriginalAllocatePages;
  gBS->Hdr.CRC32     = 0;

  gBS->CalculateCrc32 (gBS, sizeof (EFI_BOOT_SERVICES), &gBS->Hdr.CRC32);
}

/**
  AllocatePages Wrapper that applies the Platform Errata Patches to whichever Image is
  allocating.

  No Attempt is made to work out which Image is the OS Loader. The Patches identify
  their own Targets by Instruction Pattern, so an Image that is not the OS Loader
  simply has nothing matched in it. Each Image is therefore visited exactly once,
  recorded, and skipped from then on, which leaves a Handful of Comparisons per
  Allocation once both Images have been seen.
**/
STATIC
EFI_STATUS
EFIAPI
EarlyPatchAllocatePages (
  IN     EFI_ALLOCATE_TYPE     Type,
  IN     EFI_MEMORY_TYPE       MemoryType,
  IN     UINTN                 Pages,
  IN OUT EFI_PHYSICAL_ADDRESS *Memory)
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS Caller = (EFI_PHYSICAL_ADDRESS)(UINTN)RETURN_ADDRESS (0);
  EFI_PHYSICAL_ADDRESS Base;
  UINTN                Length;

  //
  // DIAGNOSTIC. Traces the OS Loader's Allocations so a Build that boots and one that
  // does not can be compared Call for Call. Remove once the Loader reaches
  // ExitBootServices.
  //
  // Every Allocation the Loader makes goes through its own EfiAllocatePages Wrapper, so
  // the immediate Return Address is the same one every Time and says nothing about who
  // wanted the Memory. The Callers above it are recovered by reading back down the Stack
  // and keeping the Values that land inside the Image, which are Return Addresses into
  // its Code. Offsets are printed relative to the Image Base so they can be resolved
  // against the Loader's Symbols afterwards.
  //
  if (mLastImageBase != 0 && Caller >= mLastImageBase && Caller < mLastImageEnd) {
    //
    // Read the Stack through a volatile Pointer taken from the Frame Address rather
    // than from a Local. Walking off the End of a Local is undefined, and an optimising
    // Build is free to fold every Read after the first back to that one Variable, which
    // reports the Wrapper and nothing above it.
    //
    volatile UINTN *Stack = (volatile UINTN *)(UINTN)__builtin_frame_address (0);
    UINT64          Frames[DIAG_FRAMES];
    UINTN           Found = 0;

    for (UINTN i = 0; i < DIAG_STACK_WORDS && Found < DIAG_FRAMES; i++) {
      UINTN Value = Stack[i];

      if (Value <= mLastImageBase || Value >= mLastImageEnd) {
        continue;
      }

      Value -= (UINTN)mLastImageBase;

      // Only Code can be a Return Address
      if (Value >= mLastImageCodeEnd) {
        continue;
      }

      for (UINTN j = 0; j < Found; j++) {
        if (Frames[j] == Value) {
          Value = 0;
          break;
        }
      }

      if (Value != 0) {
        Frames[Found++] = Value;
      }
    }

    while (Found < DIAG_FRAMES) {
      Frames[Found++] = 0;
    }

    //
    // Walk the Frame Pointer Chain as well. The Loader is built with Frame Pointers
    // throughout, so every Frame holds the previous one and its Return Address as a
    // Pair, which yields the exact Call Chain. The Scan above cannot do that: Return
    // Addresses left behind by earlier, deeper Calls sit on the Stack looking exactly
    // like live ones, and the last Capture picked up several Functions that cannot
    // reach this Point at all. Both are printed so they can be checked against
    // each other.
    //
    UINT64  Chain[DIAG_CHAIN_FRAMES];
    UINTN   ChainFound = 0;
    UINTN  *Fp         = (UINTN *)(UINTN)__builtin_frame_address (0);

    for (UINTN Depth = 0; Depth < DIAG_MAX_CHAIN && ChainFound < DIAG_CHAIN_FRAMES; Depth++) {
      UINTN Next;
      UINTN Lr;

      // A Frame Record is a Pair, so the Pointer to it is always Sixteen Byte aligned
      if (Fp == NULL || (((UINTN)Fp & 0xF) != 0)) {
        break;
      }

      Next = Fp[0];
      Lr   = Fp[1];

      if (Lr > mLastImageBase && Lr < mLastImageEnd) {
        UINT64 Offset = Lr - mLastImageBase;

        if (Offset < mLastImageCodeEnd) {
          Chain[ChainFound++] = Offset;
        }
      }

      // Frames only ever run towards higher Addresses
      if (Next <= (UINTN)Fp) {
        break;
      }

      Fp = (UINTN *)Next;
    }

    while (ChainFound < DIAG_CHAIN_FRAMES) {
      Chain[ChainFound++] = 0;
    }

    //
    // Do the Allocation first so the Address it handed back can be reported. Announcing
    // it beforehand left the one Call that matters unaccounted for: the Log simply
    // stopped, and there was no way to tell whether the Allocation had returned at all,
    // let alone what Memory the Loader was given.
    //
    Status = mOriginalAllocatePages (Type, MemoryType, Pages, Memory);

    DEBUG ((
      EFI_D_WARN,
      "OS Loader Allocation %ld: 0x%lx Pages  t=%d mt=%d -> 0x%lx %r  "
      "%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx\n",
      (UINT64)++mAllocationCount, (UINT64)Pages, (UINT32)Type, (UINT32)MemoryType,
      (UINT64)*Memory, Status,
      Frames[0], Frames[1], Frames[2],  Frames[3],  Frames[4],  Frames[5],
      Frames[6], Frames[7], Frames[8],  Frames[9],  Frames[10], Frames[11]
      ));

    DEBUG ((
      EFI_D_WARN,
      "                       chain  %lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx\n",
      Chain[0], Chain[1], Chain[2], Chain[3], Chain[4],
      Chain[5], Chain[6], Chain[7], Chain[8], Chain[9]
      ));

    DEBUG ((
      EFI_D_WARN,
      "                       ...    %lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx\n",
      Chain[10], Chain[11], Chain[12], Chain[13], Chain[14],
      Chain[15], Chain[16], Chain[17], Chain[18], Chain[19]
      ));

    return Status;
  }

  // Identify the Image the Call came from
  Status = LocateWinloadMemoryRange (Caller, &Base, &Length);
  if (EFI_ERROR (Status)) {
    goto exit;
  }

  // Skip Images that have already been visited
  for (UINTN i = 0; i < mPatchedImageCount; i++) {
    if (mPatchedImages[i] == Base) {
      // Remember it, so its next Allocation does not repeat the Walk
      if (i > 0) {
        mLastImageBase    = Base;
        mLastImageEnd     = Base + Length;
        mLastImageCodeEnd = ImageCodeEnd (Base, Length);
        mAllocationCount  = 0;
      }

      goto exit;
    }
  }

  // Record the Image
  mPatchedImages[mPatchedImageCount++] = Base;

  DEBUG ((EFI_D_WARN, "%a: Boot Application at 0x%lx (0x%lx Bytes)\n", __FUNCTION__, Base, (UINT64)Length));

  //
  // The Boot Manager is the first Image through here and allocates plenty on its own
  // Account, so only what comes after it is worth counting.
  //
  if (mPatchedImageCount > 1) {
    mLastImageBase    = Base;
    mLastImageEnd     = Base + Length;
    mLastImageCodeEnd = ImageCodeEnd (Base, Length);

    // This Call is the Image's first Allocation, so the Count starts at One here
    mAllocationCount = 1;
  }

  //
  // Drop Read-Only so the Patches can be written. The Attribute is deliberately not
  // put back: the Image is still running and re-applying it across the whole Range
  // would cover its Data as well, which the ExitBootServices Path can afford to do
  // only because the Loader is finished by then.
  //
  SetWinloadProtection (Base, Length, FALSE);

  ApplyPlatformErrataPatches (Base, Length);

exit:
  // Stop looking once there is no Room left to record another Image
  if (mPatchedImageCount >= MAX_EARLY_PATCH_IMAGES) {
    RemoveEarlyPatchHook ();
  }

  return mOriginalAllocatePages (Type, MemoryType, Pages, Memory);
}

//
// Service Trace
//
// DIAGNOSTIC. The Loader goes quiet after one particular Allocation and nothing has
// explained why, because every Theory so far assumed either that it stops inside its
// own Code or that it stops inside a Firmware Call, and the Trace could not tell those
// apart. Each Service below announces itself on the way in and again on the way out.
// A Log that ends on an Entry with no matching Exit names the Firmware Call that never
// returned; a Log that ends on a matching Pair says the Loader stopped in its own Code
// and no amount of Firmware Instrumentation will find it.
//
// Only the OS Loader's Calls are traced, so the Boot Manager's Traffic stays out of it.
//
#define TRACE_ENTER(Name)                                     \
  BOOLEAN Traced = (mLastImageBase != 0);                     \
  if (Traced) {                                               \
    DEBUG ((EFI_D_WARN, "  >%a\n", Name));                    \
  }

#define TRACE_LEAVE(Name, Status)                             \
  if (Traced) {                                               \
    DEBUG ((EFI_D_WARN, "  <%a %r\n", Name, Status));         \
  }

STATIC EFI_ALLOCATE_POOL        mOriginalAllocatePool;
STATIC EFI_FREE_POOL            mOriginalFreePool;
STATIC EFI_FREE_PAGES           mOriginalFreePages;
STATIC EFI_GET_MEMORY_MAP       mOriginalGetMemoryMap;
STATIC EFI_LOCATE_PROTOCOL      mOriginalLocateProtocol;
STATIC EFI_HANDLE_PROTOCOL      mOriginalHandleProtocol;
STATIC EFI_LOCATE_HANDLE_BUFFER mOriginalLocateHandleBuffer;
STATIC EFI_STALL                mOriginalStall;
STATIC EFI_OPEN_PROTOCOL        mOriginalOpenProtocol;
STATIC EFI_CLOSE_PROTOCOL       mOriginalCloseProtocol;

//
// Runtime Services.
//
// These were the Blind Spot. Ten Boot Services were traced and the OS Loader looked
// silent, which was read as the Loader wedging without ever calling the Firmware - but
// it reads UEFI Variables through the Runtime Table, which nothing here was watching.
// Build 22621 asks for SecureBoot and SetupMode in the Window where it stops and Build
// 22000 never does, so what happens in these Calls is worth seeing directly.
//
STATIC EFI_GET_VARIABLE           mOriginalGetVariable;
STATIC EFI_GET_NEXT_VARIABLE_NAME mOriginalGetNextVariableName;
STATIC EFI_SET_VARIABLE           mOriginalSetVariable;

//
// Console Output.
//
// The Logging Patch makes the OS Loader take a Path it would otherwise skip: BlLogString
// normally drops its Message, and with the Gate cleared it formats it and hands it to
// the Console at Offset Sixty Four of the System Table. That Console is ours. The Loader
// has never once produced Output through it, which is either because it never logs, or
// because it enters and does not come back - and those two look exactly alike from
// outside. This tells them apart.
//
STATIC EFI_TEXT_STRING mOriginalOutputString;

//
// Heartbeat.
//
// Every Instrument so far reports what the Loader did last, which only ever narrows the
// Stop down to the Gap after it. This one asks the opposite Question: while the Loader
// appears stopped, is it still running?
//
// A periodic Timer Event keeps firing as long as Interrupts are unmasked, so if the
// Beats continue after the Loader goes quiet then it is spinning rather than dead, and
// the Stack underneath this Callback still holds its Return Addresses. Scanning for
// Words that land inside its Image recovers the Chain it is looping in - which is the
// Thing that has been missing all along. If the Beats stop too, that is just as useful:
// it means Interrupts are masked or the Processor has left, and no Amount of Service
// Tracing was ever going to show it.
//
#define HEARTBEAT_PERIOD_100NS 20000000    // Two Seconds
#define HEARTBEAT_STACK_WORDS  4096
#define HEARTBEAT_FRAMES       8
#define HEARTBEAT_MAX_REPORTS  40

STATIC EFI_EVENT mHeartbeatEvent   = NULL;
STATIC UINTN     mHeartbeatCount   = 0;
STATIC UINTN     mHeartbeatReports = 0;

STATIC
VOID
EFIAPI
HeartbeatNotify (
  IN EFI_EVENT  Event,
  IN VOID      *Context)
{
  UINT64  Frames[HEARTBEAT_FRAMES];
  UINTN   Found = 0;

  mHeartbeatCount++;

  //
  // Nothing to say until the OS Loader is actually in Memory, and nothing worth saying
  // once it has produced enough Beats to make the Point.
  //
  if (mLastImageBase == 0 || mHeartbeatReports >= HEARTBEAT_MAX_REPORTS) {
    return;
  }

  //
  // Walk up the Stack looking for Words that fall inside the Loader's executable Range.
  // The Interrupt that got us here pushed its own Frame, so the Address the Loader was
  // executing is in there along with whatever called it.
  //
  volatile UINTN *Stack = (volatile UINTN *)(UINTN)__builtin_frame_address (0);

  for (UINTN Index = 0; Index < HEARTBEAT_STACK_WORDS && Found < HEARTBEAT_FRAMES; Index++) {
    UINTN Value = Stack[Index];

    if (Value <= mLastImageBase) {
      continue;
    }

    UINTN Offset = Value - mLastImageBase;

    if (Offset < mLastImageCodeEnd) {
      //
      // Keep only the first Sighting of each Address. A Spin Loop puts the same Handful
      // of Return Addresses on the Stack over and over.
      //
      BOOLEAN Seen = FALSE;

      for (UINTN Prev = 0; Prev < Found; Prev++) {
        if (Frames[Prev] == (UINT64)Offset) {
          Seen = TRUE;
          break;
        }
      }

      if (!Seen) {
        Frames[Found++] = (UINT64)Offset;
      }
    }
  }

  while (Found < HEARTBEAT_FRAMES) {
    Frames[Found++] = 0;
  }

  mHeartbeatReports++;

  DEBUG ((
    EFI_D_WARN,
    "HEARTBEAT %d: in OS Loader at %lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx <-%lx\n",
    (UINT32)mHeartbeatCount,
    Frames[0], Frames[1], Frames[2], Frames[3],
    Frames[4], Frames[5], Frames[6], Frames[7]
    ));
}

STATIC
EFI_STATUS
EFIAPI
TraceOutputString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  IN CHAR16                          *String)
{
  DEBUG ((EFI_D_WARN, "  >ConOut\n"));

  EFI_STATUS Status = mOriginalOutputString (This, String);

  DEBUG ((EFI_D_WARN, "  <ConOut %r [%s]\n", Status, String));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceGetVariable (
  IN     CHAR16   *VariableName,
  IN     EFI_GUID *VendorGuid,
  OUT    UINT32   *Attributes  OPTIONAL,
  IN OUT UINTN    *DataSize,
  OUT    VOID     *Data        OPTIONAL)
{
  DEBUG ((EFI_D_WARN, "  >GetVariable %s\n", VariableName));

  EFI_STATUS Status =
    mOriginalGetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);

  DEBUG ((EFI_D_WARN, "  <GetVariable %s %r\n", VariableName, Status));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceGetNextVariableName (
  IN OUT UINTN    *VariableNameSize,
  IN OUT CHAR16   *VariableName,
  IN OUT EFI_GUID *VendorGuid)
{
  DEBUG ((EFI_D_WARN, "  >GetNextVariableName\n"));

  EFI_STATUS Status =
    mOriginalGetNextVariableName (VariableNameSize, VariableName, VendorGuid);

  DEBUG ((EFI_D_WARN, "  <GetNextVariableName %r\n", Status));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceSetVariable (
  IN CHAR16   *VariableName,
  IN EFI_GUID *VendorGuid,
  IN UINT32    Attributes,
  IN UINTN     DataSize,
  IN VOID     *Data)
{
  DEBUG ((EFI_D_WARN, "  >SetVariable %s\n", VariableName));

  EFI_STATUS Status =
    mOriginalSetVariable (VariableName, VendorGuid, Attributes, DataSize, Data);

  DEBUG ((EFI_D_WARN, "  <SetVariable %s %r\n", VariableName, Status));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceAllocatePool (
  IN  EFI_MEMORY_TYPE  PoolType,
  IN  UINTN            Size,
  OUT VOID           **Buffer)
{
  TRACE_ENTER ("AllocatePool");
  EFI_STATUS Status = mOriginalAllocatePool (PoolType, Size, Buffer);
  TRACE_LEAVE ("AllocatePool", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceFreePool (
  IN VOID *Buffer)
{
  TRACE_ENTER ("FreePool");
  EFI_STATUS Status = mOriginalFreePool (Buffer);
  TRACE_LEAVE ("FreePool", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceFreePages (
  IN EFI_PHYSICAL_ADDRESS Memory,
  IN UINTN                Pages)
{
  TRACE_ENTER ("FreePages");
  EFI_STATUS Status = mOriginalFreePages (Memory, Pages);
  TRACE_LEAVE ("FreePages", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceGetMemoryMap (
  IN OUT UINTN                 *MemoryMapSize,
  IN OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
  OUT    UINTN                 *MapKey,
  OUT    UINTN                 *DescriptorSize,
  OUT    UINT32                *DescriptorVersion)
{
  TRACE_ENTER ("GetMemoryMap");
  EFI_STATUS Status = mOriginalGetMemoryMap (MemoryMapSize, MemoryMap, MapKey, DescriptorSize, DescriptorVersion);
  TRACE_LEAVE ("GetMemoryMap", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceLocateProtocol (
  IN  EFI_GUID *Protocol,
  IN  VOID     *Registration OPTIONAL,
  OUT VOID    **Interface)
{
  TRACE_ENTER ("LocateProtocol");
  EFI_STATUS Status = mOriginalLocateProtocol (Protocol, Registration, Interface);
  TRACE_LEAVE ("LocateProtocol", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceHandleProtocol (
  IN  EFI_HANDLE  Handle,
  IN  EFI_GUID   *Protocol,
  OUT VOID      **Interface)
{
  TRACE_ENTER ("HandleProtocol");
  EFI_STATUS Status = mOriginalHandleProtocol (Handle, Protocol, Interface);
  TRACE_LEAVE ("HandleProtocol", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceLocateHandleBuffer (
  IN     EFI_LOCATE_SEARCH_TYPE   SearchType,
  IN     EFI_GUID                *Protocol OPTIONAL,
  IN     VOID                    *SearchKey OPTIONAL,
  IN OUT UINTN                   *NoHandles,
  OUT    EFI_HANDLE             **Buffer)
{
  TRACE_ENTER ("LocateHandleBuffer");
  EFI_STATUS Status = mOriginalLocateHandleBuffer (SearchType, Protocol, SearchKey, NoHandles, Buffer);
  TRACE_LEAVE ("LocateHandleBuffer", Status);
  return Status;
}

//
// The Loader reaches Protocols through OpenProtocol rather than HandleProtocol, which
// an earlier Round of this Trace did not cover, so a Call that never returned there
// would not have shown up at all.
//
STATIC
EFI_STATUS
EFIAPI
TraceOpenProtocol (
  IN  EFI_HANDLE  Handle,
  IN  EFI_GUID   *Protocol,
  OUT VOID      **Interface OPTIONAL,
  IN  EFI_HANDLE  AgentHandle,
  IN  EFI_HANDLE  ControllerHandle,
  IN  UINT32      Attributes)
{
  TRACE_ENTER ("OpenProtocol");
  EFI_STATUS Status = mOriginalOpenProtocol (Handle, Protocol, Interface, AgentHandle, ControllerHandle, Attributes);
  TRACE_LEAVE ("OpenProtocol", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceCloseProtocol (
  IN EFI_HANDLE  Handle,
  IN EFI_GUID   *Protocol,
  IN EFI_HANDLE  AgentHandle,
  IN EFI_HANDLE  ControllerHandle)
{
  TRACE_ENTER ("CloseProtocol");
  EFI_STATUS Status = mOriginalCloseProtocol (Handle, Protocol, AgentHandle, ControllerHandle);
  TRACE_LEAVE ("CloseProtocol", Status);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceStall (
  IN UINTN Microseconds)
{
  TRACE_ENTER ("Stall");
  EFI_STATUS Status = mOriginalStall (Microseconds);
  TRACE_LEAVE ("Stall", Status);
  return Status;
}

//
// Block Reads get the same Treatment. Reading the Boot Image goes through this Path,
// and a Transfer that never completes is exactly what a silent Stop with Interrupts
// masked would look like from outside.
//
#define MAX_TRACED_BLOCK_IO 8

STATIC EFI_BLOCK_IO_PROTOCOL *mTracedBlockIo[MAX_TRACED_BLOCK_IO];
STATIC EFI_BLOCK_READ         mOriginalReadBlocks[MAX_TRACED_BLOCK_IO];
STATIC UINTN                  mTracedBlockIoCount = 0;

STATIC
EFI_STATUS
EFIAPI
TraceReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL *This,
  IN  UINT32                 MediaId,
  IN  EFI_LBA                Lba,
  IN  UINTN                  BufferSize,
  OUT VOID                  *Buffer)
{
  EFI_STATUS Status;
  UINTN      Index;

  for (Index = 0; Index < mTracedBlockIoCount; Index++) {
    if (mTracedBlockIo[Index] == This) {
      break;
    }
  }

  if (Index == mTracedBlockIoCount) {
    return EFI_DEVICE_ERROR;
  }

  if (mLastImageBase != 0) {
    DEBUG ((EFI_D_WARN, "  >ReadBlocks %d lba=0x%lx size=0x%lx\n", (UINT32)Index, (UINT64)Lba, (UINT64)BufferSize));
  }

  Status = mOriginalReadBlocks[Index] (This, MediaId, Lba, BufferSize, Buffer);

  if (mLastImageBase != 0) {
    DEBUG ((EFI_D_WARN, "  <ReadBlocks %r\n", Status));
  }

  return Status;
}

//
// Graphics Output.
//
// The Loader reaches our Frame Buffer Driver through this Protocol, and it is the last
// Thing it touches before it goes quiet: ConsoleEfiGopEnable maps the Frame Buffer -
// those are the seven Page Table Allocations that end the Log - and then calls SetMode
// once more. Nothing has ever watched these Methods, which is the same Blind Spot that
// hid the Variable Services.
//
#define MAX_TRACED_GOP 4

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL       *mTracedGop[MAX_TRACED_GOP];
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE mOriginalGopQueryMode[MAX_TRACED_GOP];
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   mOriginalGopSetMode[MAX_TRACED_GOP];
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT        mOriginalGopBlt[MAX_TRACED_GOP];
STATIC UINTN                                   mTracedGopCount = 0;

STATIC
UINTN
FindTracedGop (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This)
{
  for (UINTN Index = 0; Index < mTracedGopCount; Index++) {
    if (mTracedGop[Index] == This) {
      return Index;
    }
  }

  return MAX_TRACED_GOP;
}

STATIC
EFI_STATUS
EFIAPI
TraceGopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                 ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  UINTN Index = FindTracedGop (This);

  if (Index == MAX_TRACED_GOP) {
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_WARN, "  >GopQueryMode %d\n", ModeNumber));
  EFI_STATUS Status = mOriginalGopQueryMode[Index] (This, ModeNumber, SizeOfInfo, Info);
  DEBUG ((EFI_D_WARN, "  <GopQueryMode %r\n", Status));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceGopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32                        ModeNumber)
{
  UINTN Index = FindTracedGop (This);

  if (Index == MAX_TRACED_GOP) {
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_WARN, "  >GopSetMode %d\n", ModeNumber));
  EFI_STATUS Status = mOriginalGopSetMode[Index] (This, ModeNumber);
  DEBUG ((EFI_D_WARN, "  <GopSetMode %r\n", Status));

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TraceGopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL      *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta       OPTIONAL)
{
  UINTN Index = FindTracedGop (This);

  if (Index == MAX_TRACED_GOP) {
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((EFI_D_WARN, "  >GopBlt op=%d %dx%d\n", (UINT32)BltOperation, (UINT32)Width, (UINT32)Height));
  EFI_STATUS Status = mOriginalGopBlt[Index] (
                        This, BltBuffer, BltOperation, SourceX, SourceY,
                        DestinationX, DestinationY, Width, Height, Delta);
  DEBUG ((EFI_D_WARN, "  <GopBlt %r\n", Status));

  return Status;
}

/**
  Put the Trace in Place on every Graphics Output the Platform has.
**/
STATIC
VOID
InstallGraphicsOutputTrace (VOID)
{
  EFI_STATUS   Status;
  EFI_HANDLE  *Handles;
  UINTN        HandleCount;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (UINTN i = 0; i < HandleCount && mTracedGopCount < MAX_TRACED_GOP; i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    Status = gBS->HandleProtocol (Handles[i], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
    if (EFI_ERROR (Status) || Gop->SetMode == TraceGopSetMode) {
      continue;
    }

    mTracedGop[mTracedGopCount]            = Gop;
    mOriginalGopQueryMode[mTracedGopCount] = Gop->QueryMode;
    mOriginalGopSetMode[mTracedGopCount]   = Gop->SetMode;
    mOriginalGopBlt[mTracedGopCount]       = Gop->Blt;

    Gop->QueryMode = TraceGopQueryMode;
    Gop->SetMode   = TraceGopSetMode;
    Gop->Blt       = TraceGopBlt;

    mTracedGopCount++;
  }

  gBS->FreePool (Handles);

  DEBUG ((EFI_D_WARN, "InstallGraphicsOutputTrace: Watching %d Graphics Output Protocol(s)\n", (UINT32)mTracedGopCount));
}

/**
  Put the Trace in Place on every Block Io the Platform has.
**/
STATIC
VOID
InstallBlockIoTrace (VOID)
{
  EFI_STATUS   Status;
  EFI_HANDLE  *Handles;
  UINTN        HandleCount;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (UINTN i = 0; i < HandleCount && mTracedBlockIoCount < MAX_TRACED_BLOCK_IO; i++) {
    EFI_BLOCK_IO_PROTOCOL *BlockIo;

    Status = gBS->HandleProtocol (Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
    if (EFI_ERROR (Status) || BlockIo->ReadBlocks == TraceReadBlocks) {
      continue;
    }

    mTracedBlockIo[mTracedBlockIoCount]      = BlockIo;
    mOriginalReadBlocks[mTracedBlockIoCount] = BlockIo->ReadBlocks;
    mTracedBlockIoCount++;

    BlockIo->ReadBlocks = TraceReadBlocks;
  }

  DEBUG ((EFI_D_WARN, "%a: Tracing %d Block Io Instance(s)\n", __FUNCTION__, (UINT32)mTracedBlockIoCount));

  gBS->FreePool (Handles);
}

//
// Generic Timer Readouts.
//
// Read inline rather than through an Assembly File of their own. Two Builds were lost
// to Output that never appeared, and a separate Source File brings a new INF Entry and
// a new exported Symbol with it - none of which this needs.
//
#define DIAG_READ_SYSREG(Name)                                          \
  ({                                                                    \
    UINT64 DiagValue;                                                   \
    __asm__ volatile ("mrs %0, " Name : "=r" (DiagValue));              \
    DiagValue;                                                          \
  })

#define DIAG_READ_COUNTER(Name)                                         \
  ({                                                                    \
    UINT64 DiagValue;                                                   \
    __asm__ volatile ("isb\n\tmrs %0, " Name                            \
                      : "=r" (DiagValue) :: "memory");                  \
    DiagValue;                                                          \
  })

/**
  Report what the Generic Timer looks like to whoever reads it next.

  DIAGNOSTIC. This Platform times itself off the Physical Counter, so nothing here has
  ever exercised the Virtual one. Windows 10 and Windows 11 21H2 never read it either,
  which is why they are unaffected by whatever State it is in; from 22H2 the Loader
  reads CNTVCT_EL0 in a couple of dozen Places. Both Counters are sampled across a
  known Delay so it is plain whether they run, run together, and run at the Rate the
  Frequency Register claims.
**/
STATIC
VOID
ReportGenericTimerState (VOID)
{
  UINT64 Frequency = DIAG_READ_SYSREG ("cntfrq_el0");
  UINT64 CurrentEl = (DIAG_READ_SYSREG ("CurrentEL") >> 2) & 0x3;
  UINT64 PhysicalStart;
  UINT64 VirtualStart;
  UINT64 PhysicalEnd;
  UINT64 VirtualEnd;

  PhysicalStart = DIAG_READ_COUNTER ("cntpct_el0");
  VirtualStart  = DIAG_READ_COUNTER ("cntvct_el0");

  // Ten Milliseconds, long enough that the Tick Counts are unambiguous
  gBS->Stall (10000);

  PhysicalEnd = DIAG_READ_COUNTER ("cntpct_el0");
  VirtualEnd  = DIAG_READ_COUNTER ("cntvct_el0");

  DEBUG ((EFI_D_WARN, "Generic Timer: EL%ld  CNTFRQ_EL0 = %ld Hz\n", CurrentEl, Frequency));

  if (CurrentEl >= 2) {
    DEBUG ((EFI_D_WARN, "Generic Timer: CNTVOFF_EL2 = 0x%lx\n", DIAG_READ_SYSREG ("cntvoff_el2")));
  }

  DEBUG ((
    EFI_D_WARN, "Generic Timer: CNTPCT 0x%lx -> 0x%lx (%ld ticks in 10 ms)\n",
    PhysicalStart, PhysicalEnd, PhysicalEnd - PhysicalStart
    ));

  DEBUG ((
    EFI_D_WARN, "Generic Timer: CNTVCT 0x%lx -> 0x%lx (%ld ticks in 10 ms)\n",
    VirtualStart, VirtualEnd, VirtualEnd - VirtualStart
    ));

  //
  // At a correct Frequency ten Milliseconds is a hundredth of it. Saying so here means
  // the Log answers the Question on its own instead of needing the Arithmetic done by
  // hand afterwards.
  //
  DEBUG ((
    EFI_D_WARN, "Generic Timer: expected about %ld ticks; virtual counter %a physical\n",
    Frequency / 100, (VirtualEnd - VirtualStart) == (PhysicalEnd - PhysicalStart) ? "matches" : "DIFFERS from"
    ));
}

EFI_STATUS
EFIAPI
KernelErrataPatcherExitBootServices (
  IN EFI_HANDLE           ImageHandle,
  IN UINTN                MapKey,
  IN EFI_PHYSICAL_ADDRESS fwpKernelSetupPhase1)
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  WinloadBase;
  UINTN                 WinloadLength;
  UINT8                *TransferToKernelShellCode;
  UINTN                 TransferToKernelShellCodeSize;

  // Restore Original EBS
  gBS->ExitBootServices = mOriginalEfiExitBootServices;
  gBS->Hdr.CRC32        = 0;

  // Calculate new CRC32
  gBS->CalculateCrc32 (gBS, sizeof (EFI_BOOT_SERVICES), &gBS->Hdr.CRC32);

  // Restore Original Allocate Pages, in Case the Early Hook is still installed
  if (gBS->AllocatePages == EarlyPatchAllocatePages) {
    RemoveEarlyPatchHook ();
  }

  //
  // Put the Runtime Table back as well. These Services outlive Boot Services, so a Hook
  // left in Place here would still be pointing at Code the Kernel has every Right to
  // reuse the Memory of.
  //
  if (mOriginalGetVariable != NULL) {
    gRT->GetVariable         = mOriginalGetVariable;
    gRT->GetNextVariableName = mOriginalGetNextVariableName;
    gRT->SetVariable         = mOriginalSetVariable;
    gRT->Hdr.CRC32           = 0;

    gBS->CalculateCrc32 (gRT, sizeof (EFI_RUNTIME_SERVICES), &gRT->Hdr.CRC32);
  }

  // Put the Console back before the Kernel inherits it
  if (mOriginalOutputString != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString = mOriginalOutputString;
  }

  // Locate Winload Memory Range
  Status = LocateWinloadMemoryRange (fwpKernelSetupPhase1, &WinloadBase, &WinloadLength);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate Winload Memory Range! Status = %r\n", __FUNCTION__, Status));
    goto exit;
  }

  // Unprotect winload.efi
  Status = SetWinloadProtection (WinloadBase, WinloadLength, FALSE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Unprotect Winload! Status = %r\n", __FUNCTION__, Status));
    goto reprotect;
  }

  // Apply Platform Errata Patches
  Status = ApplyPlatformErrataPatches (WinloadBase, WinloadLength);
  if (EFI_ERROR (Status)) {
    goto reprotect;
  }

  // Get Platform Shell Code
  GetPlatformTransferToKernelShellCode (&TransferToKernelShellCode, &TransferToKernelShellCodeSize);
  if (TransferToKernelShellCode == NULL || TransferToKernelShellCodeSize == 0) {
    goto reprotect;
  }

  // Inject Shell Code
  Status = PatchOsLoaderArm64TransferToKernel (WinloadBase, WinloadLength, TransferToKernelShellCode, TransferToKernelShellCodeSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Inject Shell Code! Status = %r\n", __FUNCTION__, Status));
  }

reprotect:
  // Protect winload.efi
  Status = SetWinloadProtection (WinloadBase, WinloadLength, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Protect Winload! Status = %r\n", __FUNCTION__, Status));
  }

exit:
  // Call Original EBS
  return gBS->ExitBootServices (ImageHandle, MapKey);
}

VOID
EFIAPI
ReadyToBootHandler (
  IN EFI_EVENT  Event,
  IN VOID      *Context)
{
  // DIAGNOSTIC. Report the Generic Timer before the OS Loader gets to read it
  ReportGenericTimerState ();

  // Save Original EBS
  mOriginalEfiExitBootServices = gBS->ExitBootServices;

  // Hook into EBS
  gBS->ExitBootServices = ExitBootServicesWrapper;

  // Hook into Allocate Pages, to reach the OS Loader before it gets to EBS
  mOriginalAllocatePages = gBS->AllocatePages;
  gBS->AllocatePages     = EarlyPatchAllocatePages;

  // DIAGNOSTIC. Announce every Service the OS Loader enters and leaves
  mOriginalAllocatePool       = gBS->AllocatePool;
  mOriginalFreePool           = gBS->FreePool;
  mOriginalFreePages          = gBS->FreePages;
  mOriginalGetMemoryMap       = gBS->GetMemoryMap;
  mOriginalLocateProtocol     = gBS->LocateProtocol;
  mOriginalHandleProtocol     = gBS->HandleProtocol;
  mOriginalLocateHandleBuffer = gBS->LocateHandleBuffer;
  mOriginalStall              = gBS->Stall;
  mOriginalOpenProtocol       = gBS->OpenProtocol;
  mOriginalCloseProtocol      = gBS->CloseProtocol;

  InstallBlockIoTrace ();

  gBS->AllocatePool       = TraceAllocatePool;
  gBS->FreePool           = TraceFreePool;
  gBS->FreePages          = TraceFreePages;
  gBS->GetMemoryMap       = TraceGetMemoryMap;
  gBS->LocateProtocol     = TraceLocateProtocol;
  gBS->HandleProtocol     = TraceHandleProtocol;
  gBS->LocateHandleBuffer = TraceLocateHandleBuffer;
  gBS->Stall              = TraceStall;
  gBS->OpenProtocol       = TraceOpenProtocol;
  gBS->CloseProtocol      = TraceCloseProtocol;

  gBS->Hdr.CRC32 = 0;

  // Calculate new CRC32
  gBS->CalculateCrc32 (gBS, sizeof (EFI_BOOT_SERVICES), &gBS->Hdr.CRC32);

  // DIAGNOSTIC. Watch the Variable Services the OS Loader actually uses
  mOriginalGetVariable         = gRT->GetVariable;
  mOriginalGetNextVariableName = gRT->GetNextVariableName;
  mOriginalSetVariable         = gRT->SetVariable;

  gRT->GetVariable         = TraceGetVariable;
  gRT->GetNextVariableName = TraceGetNextVariableName;
  gRT->SetVariable         = TraceSetVariable;

  //
  // The Runtime Table carries its own Checksum and the OS Loader is entitled to check
  // it, so it has to be brought back into Agreement after the Table is changed.
  //
  gRT->Hdr.CRC32 = 0;

  gBS->CalculateCrc32 (gRT, sizeof (EFI_RUNTIME_SERVICES), &gRT->Hdr.CRC32);

  //
  // DIAGNOSTIC. Watch the Console the Logging Patch redirects the Loader into.
  //
  // Only the Protocol's own Function Pointer moves here, not the System Table, so the
  // Table's Checksum still describes what it did before.
  //
  if (gST->ConOut != NULL) {
    mOriginalOutputString = gST->ConOut->OutputString;
    gST->ConOut->OutputString = TraceOutputString;
  }

  // DIAGNOSTIC. Watch the Graphics Output the Loader drives its Console through
  InstallGraphicsOutputTrace ();

  //
  // DIAGNOSTIC. Start the Heartbeat.
  //
  // Signalled from the Timer Interrupt, so it keeps reporting for as long as the
  // Processor is still taking Interrupts - including while the Loader is wedged.
  //
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, HeartbeatNotify, NULL, &mHeartbeatEvent);
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (mHeartbeatEvent, TimerPeriodic, HEARTBEAT_PERIOD_100NS);
  } else {
    DEBUG ((EFI_D_ERROR, "%a: Failed to start the Heartbeat! Status = %r\n", __FUNCTION__, Status));
  }

  // Close Event
  gBS->CloseEvent (Event);
}

EFI_STATUS
EFIAPI
KernelErrataPatcherLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_EVENT  ReadyToBootEvent;

  // Create Ready To Boot Event
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReadyToBootHandler, NULL, &gEfiEventReadyToBootGuid, &ReadyToBootEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Create Ready To Boot Event! Status = %r\n", __FUNCTION__, Status));
    return EFI_SUCCESS;
  }

  // Locate Memory Attribute Protocol
  return LocateMemoryAttributeProtocol ();
}
