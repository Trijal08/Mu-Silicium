/**
  Copyright (c) 2022-2023 DuoWoA authors
  SPDX-License-Identifier: MIT
**/

#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/ErrataPatchesLib.h>

#include <IndustryStandard/PeImage.h>

#include "ShellCode.h"

//
// Speculative Skips
//
// Set to one to re-enable the Patches that make the Loader step over Work it would
// normally do - Boot Resources, System Integrity, the Consoles, Display Initialisation.
// Each was picked because it looked like a likely Culprit rather than because anything
// pointed at it, and each changes the Loader's Behaviour, so they stay off while the
// Failure is being measured. They are kept because they are verified against both
// Builds and are worth trying again once the Evidence says where to aim.
//
#define WINLOAD_SPECULATIVE_SKIPS 0

//
// AArch64 Encodings
//
#define ARM64_SMC_0        0xD4000003  // smc #0
#define ARM64_MOVN_X0      0x92800000  // movn x0, #0  ->  x0 = -1
#define ARM64_MOV_W0_FAIL  0x32020BE0  // mov w0, #0xC0000001 (STATUS_UNSUCCESSFUL)
#define ARM64_RET          0xD65F03C0  // ret

//
// Prologue of the Loader's MUI Resource Initialiser.
//
// Nine Instructions, byte for byte the same in Builds 22000 and 22621 and unique in
// both Images, which is enough to find the Function without relying on an Offset:
//
//   stp x19, x20, [sp, #-80]!      stp x29, x30, [sp, #-80]!
//   stp x21, x22, [sp, #16]        mov x29, sp
//   stp x23, x24, [sp, #32]        sub sp, sp, #0x30
//   stp x25, x26, [sp, #48]        mov w8, #0xffffffff
//   str x27, [sp, #64]
//
#define WINLOAD_MUI_INIT_PROLOGUE                                      \
  "F3 53 BB A9 F5 5B 01 A9 F7 63 02 A9 F9 6B 03 A9 FB 23 00 F9 "       \
  "FD 7B BB A9 FD 03 00 91 FF C3 00 D1 08 00 80 12"

#define WINLOAD_MUI_INIT_PROLOGUE_SIZE 36

#define ARM64_NOP 0xD503201F

//
// The Loader's Logging Gate.
//
// BlLogString decides where a Message goes by testing two Bits of a Flags Word:
//
//   ldr  x8, [x20, #Offset]
//   tbz  w8, #4, .           -> skips BlDisplayPrintStringArgList (the Frame Buffer)
//   ...
//   ldr  x8, [x20, #Offset]
//   tbz  w8, #5, .           -> skips EfiPrintfArgList (the Firmware Console)
//
// Bit Five is the one worth having. EfiPrintfArgList formats the Message and hands it
// to the Console at Offset Sixty Four of the System Table, which on this Platform
// includes the Serial Port, so clearing that Test gives the Loader's own Diagnostics
// on the Wire. The Offset in the two Loads differs between Builds and the Branches are
// relative, so both are wildcarded; everything else is fixed.
//
#define WINLOAD_LOG_GATE_PATTERN                                       \
  "A8 00 20 36 E1 03 13 AA ? ? ? ? E0 0B 40 F9 ? ? ? ? 68 00 28 36"

#define WINLOAD_LOG_GATE_SIZE 24

//
// Offset of the second Test within that Pattern.
//
#define WINLOAD_LOG_GATE_TBZ_OFFSET 20

#define ARM64_MOV_W0_0 0x52800000  // mov w0, #0 (STATUS_SUCCESS)

//
// Prologue of the Loader's System Integrity Initialiser.
//
// Identical in Builds 22000 and 22621 apart from one Branch Displacement, which is
// wildcarded:
//
//   stp x19, x20, [sp, #-32]!     mov x29, sp
//   str x21, [sp, #16]            cbnz w0, .
//   stp x29, x30, [sp, #-16]!     bl .            <- Displacement differs
//                                 b .
//                                 cmp w0, #1
//
#define WINLOAD_SI_INIT_PROLOGUE                                              \
  "F3 53 BE A9 F5 0B 00 F9 FD 7B BF A9 FD 03 00 91 60 00 00 35 "              \
  "? ? ? ? 44 00 00 14 1F 04 00 71"

#define WINLOAD_SI_INIT_PROLOGUE_SIZE 32

#define ARM64_STR_XZR_X0 0xF900001F  // str xzr, [x0]

//
// Prologue of the Loader's Graphical Console Constructor.
//
// The Body differs between Builds only where it forms Addresses, so those Words are
// wildcarded and the Rest pins it down:
//
//   stp x19, x20, [sp, #-32]!     mov x21, x0
//   stp x21, x22, [sp, #16]       cmp w0, #1
//   stp x29, x30, [sp, #-16]!
//   mov x29, sp
//
#define WINLOAD_GFX_CONSOLE_PROLOGUE                                          \
  "F3 53 BE A9 F5 5B 01 A9 FD 7B BF A9 FD 03 00 91 "                          \
  "? ? ? ? ? ? ? ? F5 03 00 AA 1F 05 00 71"

#define WINLOAD_GFX_CONSOLE_PROLOGUE_SIZE 32

#define ARM64_STR_XZR_X1 0xF900003F  // str xzr, [x1]

//
// Prologue of the Loader's Remote Console Constructor.
//
// The same Shape as the Graphical one but for the sixth Word, where this takes its out
// Parameter in x1 rather than x0 - which is also what keeps the two Patterns apart.
//
#define WINLOAD_REMOTE_CONSOLE_PROLOGUE                                       \
  "F3 53 BE A9 F5 5B 01 A9 FD 7B BF A9 FD 03 00 91 "                          \
  "? ? ? ? ? ? ? ? F5 03 01 AA 1F 05 00 71"

#define WINLOAD_REMOTE_CONSOLE_PROLOGUE_SIZE 32

//
// Prologue of the Loader's Display Initialiser.
//
// Byte for byte the same in both Builds. It opens like the MUI Initialiser and parts
// company at the seventh Word, so the Length matters: these two must not be allowed to
// match each other.
//
#define WINLOAD_DISPLAY_INIT_PROLOGUE                                         \
  "F3 53 BB A9 F5 5B 01 A9 F7 63 02 A9 F9 6B 03 A9 "                          \
  "FB 23 00 F9 FD 7B B8 A9 FD 03 00 91 F3 03 00 2A"

#define WINLOAD_DISPLAY_INIT_PROLOGUE_SIZE 32

#define IN_RANGE(x, a, b)  (x >= a && x <= b)
#define GET_BITS(x)        (IN_RANGE ((x & (~0x20)), 'A', 'F') ? ((x & (~0x20)) - 'A' + 0xA) : (IN_RANGE (x, '0', '9') ? x - '0' : 0))
#define GET_BYTE(a, b)     (GET_BITS (a) << 4 | GET_BITS (b))

#define MAX_PATTERN_BYTES 64

/**
  Find a Byte Pattern in an Image's executable Sections.

  Only Code is searched. A Pattern this long is unlikely to occur in Data by Chance, but
  Data that happened to match would be rewritten, and the Sections say plainly which is
  which. The Search steps an Instruction at a Time, which every Pattern here is aligned
  to.

  @param  Base         Base Address of the loaded Image.
  @param  Length       Size of that Image in Bytes.
  @param  Pattern      Hex Bytes as Text, separated by Spaces.
  @param  PatternSize  How many Bytes that Text describes.

  @return Address of the first Match, or zero if there is none.
**/
STATIC
EFI_PHYSICAL_ADDRESS
FindPatternInCode (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINTN                 Length,
  IN CONST CHAR8          *Pattern,
  IN UINTN                 PatternSize)
{
  EFI_IMAGE_DOS_HEADER     *DosHeader = (EFI_IMAGE_DOS_HEADER *)(UINTN)Base;
  EFI_IMAGE_NT_HEADERS64   *NtHeader;
  EFI_IMAGE_SECTION_HEADER *Section;
  UINT8                     Bytes[MAX_PATTERN_BYTES];
  UINT8                     Known[MAX_PATTERN_BYTES];
  CONST CHAR8              *Cursor = Pattern;
  UINTN                     Count  = 0;

  if (PatternSize == 0 || PatternSize > MAX_PATTERN_BYTES) {
    return 0;
  }

  //
  // Read the Pattern one Token at a Time. A Token is either two Hex Digits or a
  // Question Mark standing for a Byte whose Value does not matter, which is what lets
  // one Pattern cover Builds that differ only in a Branch Target or a Structure Offset.
  //
  while (*Cursor != '\0' && Count < PatternSize) {
    if (*Cursor == ' ') {
      Cursor++;
      continue;
    }

    if (*Cursor == '\?') {
      Bytes[Count] = 0;
      Known[Count] = 0;
      Cursor++;
    } else {
      Bytes[Count] = (UINT8)GET_BYTE (Cursor[0], Cursor[1]);
      Known[Count] = 1;
      Cursor += 2;
    }

    Count++;
  }

  // A Pattern that does not describe the Number of Bytes it claims is not usable
  if (Count != PatternSize) {
    return 0;
  }

  if (DosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE || DosHeader->e_lfanew >= Length) {
    return 0;
  }

  NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(UINTN)(Base + DosHeader->e_lfanew);
  if (NtHeader->Signature != EFI_IMAGE_NT_SIGNATURE) {
    return 0;
  }

  Section = (EFI_IMAGE_SECTION_HEADER *)(UINTN)((UINTN)NtHeader + sizeof (UINT32) +
                                                sizeof (EFI_IMAGE_FILE_HEADER) +
                                                NtHeader->FileHeader.SizeOfOptionalHeader);

  for (UINT16 i = 0; i < NtHeader->FileHeader.NumberOfSections; i++, Section++) {
    UINT8 *Code;
    UINTN  Size;

    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0 ||
        Section->VirtualAddress >= Length)
    {
      continue;
    }

    Code = (UINT8 *)(UINTN)(Base + Section->VirtualAddress);
    Size = MIN (Section->Misc.VirtualSize, Length - Section->VirtualAddress);

    if (Size < PatternSize) {
      continue;
    }

    for (UINTN Offset = 0; Offset + PatternSize <= Size; Offset += sizeof (UINT32)) {
      UINTN Index;

      for (Index = 0; Index < PatternSize; Index++) {
        if (Known[Index] && Code[Offset + Index] != Bytes[Index]) {
          break;
        }
      }

      if (Index == PatternSize) {
        return (EFI_PHYSICAL_ADDRESS)(UINTN)&Code[Offset];
      }
    }
  }

  return 0;
}

/**
  Stop the Windows Boot Loader from issuing Secure Monitor Calls.

  From Windows 11 22H2 (Build 22621) the Loader talks to the Secure Monitor on two
  Occasions before it hands the Machine over. It probes for Qualcomm Secure Launch from
  OslPrepareTarget, and it registers a PSCI Handler which in 21H2 belonged to the Secure
  Launch Module alone and now sits in the general Boot Library, so it runs on an
  ordinary Boot. Exynos answers neither Call. The Core enters the Secure Monitor and
  never comes back, which from the Firmware's Side looks like the Loader simply going
  quiet: no further Boot Service Calls, no Exception, and Interrupts masked the whole
  Time. Windows 11 21H2 boots because it makes neither Call, and Qualcomm Devices are
  unaffected because their Monitor implements both.

  Every one of those Paths ends at one of exactly two Instructions. The Loader keeps its
  Secure Monitor Calls in two small Stubs near the Start of its Code, one issuing
  generic Calls on behalf of PSCI and one issuing the Launch Call, and both were found
  unchanged in Builds 22000, 22621, 26100 and 28000. Rewriting those two Instructions is
  therefore the whole Fix, and needs no Knowledge of who is calling:

      smc #0   ->   movn x0, #0

  Minus One is what both Callers already read as failure. PSCI defines it as
  NOT_SUPPORTED, and the Launch Path treats any negative Result as an Error and reports
  Secure Launch unavailable. The surrounding Stub is untouched, so it still returns the
  Result through its out Parameters exactly as before.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
DisableSecureMonitorCalls (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_IMAGE_DOS_HEADER     *DosHeader = (EFI_IMAGE_DOS_HEADER *)(UINTN)Base;
  EFI_IMAGE_NT_HEADERS64   *NtHeader;
  EFI_IMAGE_SECTION_HEADER *Section;
  UINTN                     Patched = 0;

  // Verify the Image starts with a PE Header
  if (DosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE || DosHeader->e_lfanew >= Length) {
    return;
  }

  NtHeader = (EFI_IMAGE_NT_HEADERS64 *)(UINTN)(Base + DosHeader->e_lfanew);
  if (NtHeader->Signature != EFI_IMAGE_NT_SIGNATURE) {
    return;
  }

  // The Section Table follows the Optional Header, whose Size the File Header declares
  Section = (EFI_IMAGE_SECTION_HEADER *)(UINTN)((UINTN)NtHeader + sizeof (UINT32) +
                                                sizeof (EFI_IMAGE_FILE_HEADER) +
                                                NtHeader->FileHeader.SizeOfOptionalHeader);

  //
  // Only Code is worth looking through. The Encoding is short enough to turn up in
  // Data by Chance, and Data that happens to match must not be rewritten.
  //
  for (UINT16 i = 0; i < NtHeader->FileHeader.NumberOfSections; i++, Section++) {
    UINT32 *Code;
    UINTN   Count;

    if ((Section->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) == 0) {
      continue;
    }

    if (Section->VirtualAddress >= Length) {
      continue;
    }

    Code  = (UINT32 *)(UINTN)(Base + Section->VirtualAddress);
    Count = MIN (Section->Misc.VirtualSize, Length - Section->VirtualAddress) / sizeof (UINT32);

    for (UINTN Index = 0; Index < Count; Index++) {
      if (Code[Index] != ARM64_SMC_0) {
        continue;
      }

      // Report Failure instead of entering the Secure Monitor
      Code[Index] = ARM64_MOVN_X0;

      WriteBackInvalidateDataCacheRange ((VOID *)&Code[Index], sizeof (UINT32));
      InvalidateInstructionCacheRange   ((VOID *)&Code[Index], sizeof (UINT32));

      DEBUG ((
        EFI_D_WARN, "%a: Disabled a Secure Monitor Call at Image +0x%lx\n",
        __FUNCTION__, (UINT64)(Section->VirtualAddress + Index * sizeof (UINT32))
        ));

      Patched++;
    }
  }

  if (Patched != 0) {
    DEBUG ((EFI_D_WARN, "%a: %ld Secure Monitor Call(s) disabled\n", __FUNCTION__, (UINT64)Patched));
  }
}

#if WINLOAD_SPECULATIVE_SKIPS

/**
  Report the Loader's localised Boot Resources as unavailable.

  DIAGNOSTIC / WORKAROUND. Build 22621 stops dead inside BlInitializeLibrary while
  bringing up its MUI Resources. The Path is known exactly - BlpResourceInitialize calls
  ResInitializeMuiResources, which loads a Resource Image through BlImgLoadPEImageEx -
  and the Loader makes no further Firmware Call of any kind after the Buffer for that
  Image is allocated, so whatever goes wrong is inside its own Code and nothing on this
  Side can observe it.

  Reporting Failure from the Initialiser is a Route the Loader already takes. Its Caller
  turns the Result straight into a Boolean:

      bl   ResInitializeMuiResources
      cmp  w0, #0
      cset w19, ge

  and carries on regardless, and a few Instructions earlier there is an existing Branch
  that skips the Call altogether. The Resources themselves are Localised Strings for the
  Boot Screens, which nothing needs in order to boot. So the Loader is told they are not
  there:

      mov w0, #0xC0000001
      ret

  If the Loader then gets further, this Sub Tree is where it was stuck. If it stops at
  the same Point with the Call gone, the Fault lies beyond it and this can be removed.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
SkipBootResourceInitialisation (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Entry;

  Match = FindPatternInCode (Base, Length, WINLOAD_MUI_INIT_PROLOGUE, WINLOAD_MUI_INIT_PROLOGUE_SIZE);
  if (Match == 0) {
    //
    // Say so out loud. Staying quiet here makes a Build that never got this Code look
    // exactly like one where the Search came up empty.
    //
    DEBUG ((
      EFI_D_WARN, "%a: No Boot Resource Initialiser found in the 0x%lx Byte Image\n",
      __FUNCTION__, (UINT64)Length
      ));

    return;
  }

  Entry    = (UINT32 *)(UINTN)Match;
  Entry[0] = ARM64_MOV_W0_FAIL;
  Entry[1] = ARM64_RET;

  WriteBackInvalidateDataCacheRange ((VOID *)Entry, 2 * sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Entry, 2 * sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: Boot Resource Initialisation disabled at Image +0x%lx\n",
    __FUNCTION__, (UINT64)(Match - Base)
    ));
}

#endif

/**
  Route the Loader's own Log Messages to the Firmware Console.

  Windows keeps no plain Serial Log during Boot. What it has is a Debugger Transport,
  which speaks Packets rather than Text, and a Frame Buffer the Messages are drawn on.
  It does however carry a third Path that is simply switched off: BlLogString will hand
  a formatted Message to EfiPrintfArgList, which prints through the Console at Offset
  Sixty Four of the EFI System Table. This Platform puts the Serial Port on that
  Console, so the only Thing standing between the Loader and a readable Log on the Wire
  is one Bit Test.

  Clearing that Test costs a single Instruction and turns the Test on rather than off,
  which is the safe Direction: nothing that used to happen stops happening, one Path
  that was skipped now runs. The Message still goes to the Frame Buffer as before.

  Worth doing on a Build that boots as well as one that does not, because then the two
  Logs can be read side by side and the Point where they part company is the Answer.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
EnableLoaderConsoleLogging (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Test;

  Match = FindPatternInCode (Base, Length, WINLOAD_LOG_GATE_PATTERN, WINLOAD_LOG_GATE_SIZE);
  if (Match == 0) {
    return;
  }

  Test = (UINT32 *)(UINTN)(Match + WINLOAD_LOG_GATE_TBZ_OFFSET);

  // Already done on an earlier Pass
  if (*Test == ARM64_NOP) {
    return;
  }

  *Test = ARM64_NOP;

  WriteBackInvalidateDataCacheRange ((VOID *)Test, sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Test, sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: Loader Logging routed to the Firmware Console at Image +0x%lx\n",
    __FUNCTION__, (UINT64)((UINTN)Test - Base)
    ));
}

#if WINLOAD_SPECULATIVE_SKIPS

/**
  Report System Integrity Initialisation as done without doing it.

  DIAGNOSTIC / WORKAROUND. InitializeLibrary calls BlpDisplayInitialize and then
  BlpSiInitialize back to back, and the Loader goes quiet somewhere across the two. The
  last Allocation seen comes from inside the first, so either that one never finishes or
  the second never returns. Nothing distinguishes them from the Outside, because neither
  makes another Firmware Call.

  BlpSiInitialize is the larger unknown: eighty six Functions of Platform Attestation,
  TCG Logging and Policy Measurement, all of which expect a Trusted Platform Module this
  Device does not have. Its Caller reads the Result as a Status and carries straight on
  when it is not negative:

      bl   BlpSiInitialize
      mov  w19, w0
      tbz  w19, #31, .          <- Success continues here

  so reporting Success is the Route a Boot with nothing to measure would take anyway:

      mov w0, #0
      ret

  If the Loader then gets further, the Fault was in that Subtree. If it stops in the
  same Place, the Fault is in the Display Initialiser instead and this comes back out.
  Either Answer is worth one Boot, and one of them is a Boot.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
SkipSystemIntegrityInitialisation (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Entry;

  Match = FindPatternInCode (Base, Length, WINLOAD_SI_INIT_PROLOGUE, WINLOAD_SI_INIT_PROLOGUE_SIZE);
  if (Match == 0) {
    return;
  }

  Entry = (UINT32 *)(UINTN)Match;

  // Already done on an earlier Pass
  if (Entry[0] == ARM64_MOV_W0_0 && Entry[1] == ARM64_RET) {
    return;
  }

  Entry[0] = ARM64_MOV_W0_0;
  Entry[1] = ARM64_RET;

  WriteBackInvalidateDataCacheRange ((VOID *)Entry, 2 * sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Entry, 2 * sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: System Integrity Initialisation skipped at Image +0x%lx\n",
    __FUNCTION__, (UINT64)(Match - Base)
    ));
}

/**
  Decline the Graphical Console so the Loader falls back to the Text one.

  DIAGNOSTIC / WORKAROUND. With the System Integrity Initialiser proved innocent - it is
  never even reached - the Loader stops inside BlpDisplayInitialize, and the last
  Allocation it makes comes from the Frame Buffer Mapping deep inside the Graphical
  Console Constructor. Declining the whole Console removes that Subtree in one Piece.

  The Loader is built for Firmware with no usable Graphics: DsppInitialize keeps the
  Console it was handed only when the Pointer comes back non NULL, and otherwise calls
  ConsoleCreateLocalTextConsole instead. That Fallback prints through the Firmware
  Console, which on this Platform carries the Serial Port, so the Loader's Output should
  survive - and with the Logging Gate already opened, it should become visible.

  The Constructor writes its Result only on Success, so failing without touching the out
  Parameter would leave the Caller reading whatever was on the Stack and testing it for
  NULL. The Pointer is therefore cleared explicitly before reporting Failure:

      str xzr, [x0]
      mov w0, #0xC0000001
      ret

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
DeclineGraphicalConsole (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Entry;

  Match = FindPatternInCode (Base, Length, WINLOAD_GFX_CONSOLE_PROLOGUE, WINLOAD_GFX_CONSOLE_PROLOGUE_SIZE);
  if (Match == 0) {
    return;
  }

  Entry = (UINT32 *)(UINTN)Match;

  // Already done on an earlier Pass
  if (Entry[0] == ARM64_STR_XZR_X0) {
    return;
  }

  Entry[0] = ARM64_STR_XZR_X0;
  Entry[1] = ARM64_MOV_W0_FAIL;
  Entry[2] = ARM64_RET;

  WriteBackInvalidateDataCacheRange ((VOID *)Entry, 3 * sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Entry, 3 * sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: Graphical Console declined at Image +0x%lx\n",
    __FUNCTION__, (UINT64)(Match - Base)
    ));
}

/**
  Decline the Remote Console.

  DIAGNOSTIC / WORKAROUND. Declining the Graphical Console removed its Frame Buffer
  Mapping and with it eight Allocations, and the Loader stopped after twelve instead of
  twenty - the same Point in its Execution with the Graphics Work taken out from in
  front of it. So the Fault is not in the Graphical Console, and it is not in the System
  Integrity Initialiser, which is never reached. What is left is the Handful of Console
  Constructions that DsppInitialize performs afterwards, and of those only two run on
  both the old and the new Path.

  This is the one that touches Hardware. ConsoleRemoteConstruct reads the Firmware's
  Serial Redirection Table through BlUtlGetAcpiTable, asks for the Emergency Management
  Parameters, and opens the Serial Port itself through SpOpen. This Platform's own
  Debug Output already owns that same UART, and a second Driver programming it and then
  waiting on a Status Bit is exactly the Kind of Thing that stops without a Fault, an
  Allocation or a Firmware Call.

  Its Caller reads the Result out of the Pointer it passed and tests it for NULL, so
  clearing that Pointer and reporting Failure leaves the Loader on a Path it already
  handles. Note the out Parameter arrives in x1 here, not x0:

      str xzr, [x1]
      mov w0, #0xC0000001
      ret

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
DeclineRemoteConsole (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Entry;

  Match = FindPatternInCode (Base, Length, WINLOAD_REMOTE_CONSOLE_PROLOGUE, WINLOAD_REMOTE_CONSOLE_PROLOGUE_SIZE);
  if (Match == 0) {
    return;
  }

  Entry = (UINT32 *)(UINTN)Match;

  // Already done on an earlier Pass
  if (Entry[0] == ARM64_STR_XZR_X1) {
    return;
  }

  Entry[0] = ARM64_STR_XZR_X1;
  Entry[1] = ARM64_MOV_W0_FAIL;
  Entry[2] = ARM64_RET;

  WriteBackInvalidateDataCacheRange ((VOID *)Entry, 3 * sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Entry, 3 * sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: Remote Console declined at Image +0x%lx\n",
    __FUNCTION__, (UINT64)(Match - Base)
    ));
}

/**
  Skip Display Initialisation entirely.

  DIAGNOSTIC / WORKAROUND. Declining the Consoles one at a Time has been moving the Stop
  Point forward a Step at a Time - twenty Allocations, then twelve once the Graphical
  Console went, then thirteen once the Remote one did - which says there is more than
  one Thing wrong in here rather than a single Culprit. Rather than keep peeling them
  off, this removes the whole Subtree in one Go: every Console the Loader builds, the
  Splitters that combine them, and the Boot Options it reads along the Way.

  The Caller can afford it. BlpDisplayInitialize does nothing but tail call this, and
  InitializeLibrary throws the Result away - the very next Instruction after the Call
  loads w0 with one for the Argument to the next Function. So the Value returned here
  reaches nobody, and Success is the honest Answer to give a Caller that is not looking.

  The Loader's own Logging does not depend on any of this: EfiPrintfArgList prints
  through the Console in the EFI System Table, which the Firmware owns, so whatever the
  Loader has to say still arrives.

  @param  Base    Base Address of the loaded Image.
  @param  Length  Size of that Image in Bytes.
**/
STATIC
VOID
SkipDisplayInitialisation (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  EFI_PHYSICAL_ADDRESS Match;
  UINT32              *Entry;

  Match = FindPatternInCode (Base, Length, WINLOAD_DISPLAY_INIT_PROLOGUE, WINLOAD_DISPLAY_INIT_PROLOGUE_SIZE);
  if (Match == 0) {
    return;
  }

  Entry = (UINT32 *)(UINTN)Match;

  // Already done on an earlier Pass
  if (Entry[0] == ARM64_MOV_W0_0 && Entry[1] == ARM64_RET) {
    return;
  }

  Entry[0] = ARM64_MOV_W0_0;
  Entry[1] = ARM64_RET;

  WriteBackInvalidateDataCacheRange ((VOID *)Entry, 2 * sizeof (UINT32));
  InvalidateInstructionCacheRange   ((VOID *)Entry, 2 * sizeof (UINT32));

  DEBUG ((
    EFI_D_WARN, "%a: Display Initialisation skipped at Image +0x%lx\n",
    __FUNCTION__, (UINT64)(Match - Base)
    ));
}

#endif

EFI_STATUS
ApplyPlatformErrataPatches (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  //
  // Work out which Image this is before touching anything.
  //
  // The Boot Manager and the OS Loader are built from the same Sources and share most
  // of this Code, but they must not be treated the same: the Boot Manager does not
  // survive losing its Display Initialiser - it stops before it can even report that
  // its Library came up, and never reaches the Point of loading anything. The System
  // Integrity Initialiser tells them apart, being present only in the Loader.
  //
  // This has to be decided first, because the Patches below overwrite the very Prologue
  // the Test looks for.
  //
  BOOLEAN IsOsLoader =
    (BOOLEAN)(FindPatternInCode (Base, Length, WINLOAD_SI_INIT_PROLOGUE, WINLOAD_SI_INIT_PROLOGUE_SIZE) != 0);

  DisableSecureMonitorCalls (Base, Length);
  EnableLoaderConsoleLogging (Base, Length);

  //
  // Everything below here was chosen by Guesswork - each one a Function that looked
  // like a plausible Culprit rather than one the Evidence named - and every one of them
  // changes what the Loader does. Measuring a Loader that has already been altered four
  // Ways answers Questions about the Alterations, not about the Fault, so they are off
  // while the Call Chain is being read. The two Patches left are safe to keep: the
  // Secure Monitor one is proven to change nothing here, and the Logging one only adds
  // an Output Path.
  //
#if WINLOAD_SPECULATIVE_SKIPS
  SkipBootResourceInitialisation (Base, Length);

  if (IsOsLoader) {
    SkipSystemIntegrityInitialisation (Base, Length);

    //
    // Skipping the Display Initialiser already covers both of these - neither Console is
    // reached once it returns immediately - but they are applied anyway so the Log still
    // reports whether their Patterns match, which is worth knowing if the Display Patch
    // is ever taken back out.
    //
    DeclineGraphicalConsole (Base, Length);
    DeclineRemoteConsole (Base, Length);
    SkipDisplayInitialisation (Base, Length);
  }
#else
  (VOID)IsOsLoader;
#endif

  return EFI_SUCCESS;
}

VOID
GetPlatformTransferToKernelShellCode (
  OUT UINT8 **ShellCode,
  OUT UINTN  *ShellCodeSize)
{
  // Pass Shell Code Data
  *ShellCode     = TransferToKernelShellCode;
  *ShellCodeSize = sizeof (TransferToKernelShellCode);
}
