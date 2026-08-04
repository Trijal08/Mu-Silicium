/**
  Copyright (c) 2009 Samsung Electronics Co., Ltd.
  Copyright (c) 2024, Google LLC. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EFISpi.h>
#include <Protocol/EFIUsi.h>

#include "Spi.h"

//
// Global Variables
//
STATIC EFI_USI_PROTOCOL *mUsiProtocol;

//
// Per Bus State. Only the Buses actually Initialised are Tracked, and the
// Controller Address is Cached so every Transfer does not have to ask the USI
// Protocol for it again.
//
#define SPI_MAX_TRACKED_BUSES 4

typedef struct {
  BOOLEAN              InUse;
  UINT8                BusNumber;
  EFI_PHYSICAL_ADDRESS Address;
  BOOLEAN              ManualChipSelect;
  BOOLEAN              Measured;
} SPI_BUS_STATE;

STATIC SPI_BUS_STATE mBusState[SPI_MAX_TRACKED_BUSES];

STATIC
SPI_BUS_STATE *
FindBus (IN UINT8 BusNumber)
{
  for (UINT8 i = 0; i < SPI_MAX_TRACKED_BUSES; i++) {
    if (mBusState[i].InUse && mBusState[i].BusNumber == BusNumber) {
      return &mBusState[i];
    }
  }

  return NULL;
}

STATIC
SPI_BUS_STATE *
AllocateBus (IN UINT8 BusNumber)
{
  SPI_BUS_STATE *Bus = FindBus (BusNumber);

  // Reuse the Entry if this Bus was already Initialised
  if (Bus != NULL) {
    return Bus;
  }

  for (UINT8 i = 0; i < SPI_MAX_TRACKED_BUSES; i++) {
    if (!mBusState[i].InUse) {
      mBusState[i].InUse     = TRUE;
      mBusState[i].BusNumber = BusNumber;

      return &mBusState[i];
    }
  }

  return NULL;
}

/**
  Drains both FIFOs and Returns the Channel to a Known State.

  This follows the Kernel's Flush Routine: Stop both Channels, Zero the Packet
  Count, hold the Channel in Software Reset while Draining, then Release the
  Reset and Clear the DMA Bits. High Speed is Cleared along with the Reset
  because it must not be left set for a Slow Transfer.
**/
STATIC
VOID
SpiFlushFifo (IN EFI_PHYSICAL_ADDRESS Address)
{
  UINT32 Value;
  UINTN  Timeout;

  // Stop both Channels
  Value  = MmioRead32 (Address + SPI_CH_CFG);
  Value &= ~(SPI_CH_RXCH_ON | SPI_CH_TXCH_ON);
  MmioWrite32 (Address + SPI_CH_CFG, Value);

  MmioWrite32 (Address + SPI_PACKET_CNT, 0);

  // Enter Software Reset
  Value  = MmioRead32 (Address + SPI_CH_CFG);
  Value |= SPI_CH_SW_RST;
  Value &= ~SPI_CH_HS_EN;
  MmioWrite32 (Address + SPI_CH_CFG, Value);

  //
  // Note the Loop Shape here. A Post Decrement in the Condition leaves the
  // Counter Wrapped rather than at Zero once it Expires, so the Test that
  // follows would never Fire. The Count is Decremented in the Third Clause
  // instead, exactly as the Kernel does with its Pre Decrement.
  //

  // Drain the Transmit FIFO
  for (Timeout = SPI_FLUSH_TIMEOUT_US; Timeout > 0; Timeout--) {
    if (!SPI_TX_FIFO_LVL (MmioRead32 (Address + SPI_STATUS))) {
      break;
    }

    MicroSecondDelay (SPI_POLL_INTERVAL_US);
  }

  if (Timeout == 0) {
    DEBUG ((EFI_D_WARN, "SpiDxe: Timed out Flushing the Transmit FIFO!\n"));
  }

  // Drain the Receive FIFO
  for (Timeout = SPI_FLUSH_TIMEOUT_US; Timeout > 0; Timeout--) {
    if (!SPI_RX_FIFO_LVL (MmioRead32 (Address + SPI_STATUS))) {
      break;
    }

    MmioRead32 (Address + SPI_RX_DATA);
  }

  if (Timeout == 0) {
    DEBUG ((EFI_D_WARN, "SpiDxe: Timed out Flushing the Receive FIFO!\n"));
  }

  // Leave Software Reset
  Value  = MmioRead32 (Address + SPI_CH_CFG);
  Value &= ~SPI_CH_SW_RST;
  MmioWrite32 (Address + SPI_CH_CFG, Value);

  Value  = MmioRead32 (Address + SPI_MODE_CFG);
  Value &= ~(SPI_MODE_TXDMA_ON | SPI_MODE_RXDMA_ON);
  MmioWrite32 (Address + SPI_MODE_CFG, Value);
}

/**
  Brings the Controller to a Known State from whatever the Boot Loader left
  behind. This is the Kernel's Hardware Init Step and every Write in it Matters
  on a Warm Start:

  Mode Configuration is Zeroed Outright rather than Read and Modified, because
  the Bits the Boot Loader left there are not Trustworthy. The Pending Register
  is Set and then Cleared, which is how this Controller Drops Stale Error
  Latches. The Swap Configuration is Zeroed because it can Reverse Bits and
  Bytes on either Direction, and a Stale Setting there turns Perfectly Good
  Traffic into Constant looking Rubbish. The Trailing Count is Raised to its
  Maximum so a Receive Stream is not Called Finished Early.

  No Clock Source Selection is Written: on this Controller the Bit Clock comes
  from the Clock Management Unit rather than from an Internal Mux.
**/
STATIC
VOID
SpiHardwareInit (IN EFI_PHYSICAL_ADDRESS Address)
{
  UINT32 Value;

  // Mask every Interrupt Source, since Polling is used throughout
  MmioWrite32 (Address + SPI_INT_EN, 0);

  MmioWrite32 (Address + SPI_MODE_CFG, 0);
  MmioWrite32 (Address + SPI_PACKET_CNT, 0);

  // Drop Stale Error Latches
  MmioWrite32 (
    Address + SPI_PENDING_CLR,
    SPI_PND_RX_OVERRUN_CLR | SPI_PND_RX_UNDERRUN_CLR |
    SPI_PND_TX_OVERRUN_CLR | SPI_PND_TX_UNDERRUN_CLR
    );
  MmioWrite32 (Address + SPI_PENDING_CLR, 0);

  // No Bit or Byte Reversal in either Direction
  MmioWrite32 (Address + SPI_SWAP_CFG, 0);

  Value  = MmioRead32 (Address + SPI_MODE_CFG);
  Value &= ~SPI_MODE_4BURST;
  Value |= (SPI_MAX_TRAILCNT << SPI_TRAILCNT_OFFSET);
  MmioWrite32 (Address + SPI_MODE_CFG, Value);

  SpiFlushFifo (Address);
}

STATIC
VOID
SpiAssertChipSelect (
  IN SPI_BUS_STATE *Bus,
  IN BOOLEAN        Assert)
{
  if (!Bus->ManualChipSelect) {
    return;
  }

  if (Assert) {
    MmioWrite32 (Bus->Address + SPI_CS_REG, 0);

    //
    // The Board asks for a Setup Delay between the Select going Low and the
    // first Clock Edge.
    //
    MicroSecondDelay (SPI_CS_SETUP_DELAY_US);
  } else {
    MmioWrite32 (Bus->Address + SPI_CS_REG, SPI_CS_SIG_INACT);
  }
}

EFI_STATUS
EFIAPI
SpiInitBus (
  IN UINT8        BusNumber,
  IN EFI_SPI_MODE Mode,
  IN BOOLEAN      ManualChipSelect)
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS Address;
  SPI_BUS_STATE       *Bus;
  UINT32               Value;

  // Verify Mode Parameter
  if (Mode >= SPI_MODE_NUM) {
    return EFI_INVALID_PARAMETER;
  }

  // Get the Controller Address of this Bus
  Status = mUsiProtocol->GetControllerAddr (BusNumber, BUS_SPI, &Address);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SpiDxe: SPI Bus %u was not Found!\n", BusNumber));
    return Status;
  }

  // Track this Bus
  Bus = AllocateBus (BusNumber);
  if (Bus == NULL) {
    DEBUG ((EFI_D_ERROR, "SpiDxe: Out of Bus Slots!\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Bus->Address          = Address;
  Bus->ManualChipSelect = ManualChipSelect;

  // Switch the Associated USI Block into SPI Mode
  Status = mUsiProtocol->SetMode (Address, MODE_SPI);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SpiDxe: Failed to set USI Mode of SPI Bus %u!\n", BusNumber));
    return Status;
  }

  //
  // The USI Block comes up Held in Reset, so Clear that before Touching the
  // Controller behind it. The short Delay afterwards is what the Kernel's USI
  // Driver does.
  //
  Value  = MmioRead32 (Address + SPI_USI_CON);
  Value &= ~SPI_USI_CON_RESET;
  MmioWrite32 (Address + SPI_USI_CON, Value);

  MicroSecondDelay (1);

  //
  // Ask for the Clock Continuously rather than letting the Block Gate itself.
  //
  // This is a Deliberate Departure from the Kernel, which only does it when the
  // Device Tree carries "samsung,clkreq-on" and otherwise leaves the Block to
  // Hand Shake for its own Clock through Hardware Auto Clock Gating. That Hand
  // Shake needs the Clock and Power Framework on the other End of it, and there
  // is none here, so a Block left to Negotiate simply never Gets Clocked: it
  // Accepts every Register Write and Shifts nothing, with an Empty Transmit
  // FIFO and no Error Latched to Explain it.
  //
  Value  = MmioRead32 (Address + SPI_USI_OPTION);
  Value &= ~SPI_USI_OPTION_CLKSTOP_ON;
  Value |= SPI_USI_OPTION_CLKREQ_ON;
  MmioWrite32 (Address + SPI_USI_OPTION, Value);

  DEBUG ((
    EFI_D_INFO,
    "SpiDxe: USI Control 0x%08x, USI Option 0x%08x.\n",
    MmioRead32 (Address + SPI_USI_CON),
    MmioRead32 (Address + SPI_USI_OPTION)
    ));

  //
  // Scrub whatever the Boot Loader left in the Controller before Configuring
  // it. This Zeroes the Mode Register, so everything below has to follow.
  //
  SpiHardwareInit (Address);

  // Set Polarity and Phase
  Value  = MmioRead32 (Address + SPI_CH_CFG);
  Value &= ~(SPI_CH_SLAVE | SPI_CH_CPOL_L | SPI_CH_CPHA_B);

  if (Mode == SPI_MODE_2 || Mode == SPI_MODE_3) {
    Value |= SPI_CH_CPOL_L;
  }

  if (Mode == SPI_MODE_1 || Mode == SPI_MODE_3) {
    Value |= SPI_CH_CPHA_B;
  }

  MmioWrite32 (Address + SPI_CH_CFG, Value);

  //
  // Byte at a Time on both the Channel and the Bus Side. Loop Back stays Off.
  //
  Value  = MmioRead32 (Address + SPI_MODE_CFG);
  Value &= ~(SPI_MODE_BUS_TSZ_MASK | SPI_MODE_CH_TSZ_MASK | SPI_MODE_SELF_LOOPBACK);
  Value |= SPI_MODE_BUS_TSZ_BYTE | SPI_MODE_CH_TSZ_BYTE;
  MmioWrite32 (Address + SPI_MODE_CFG, Value);

  //
  // No Prescaler Programming here. On this Controller the Bit Clock comes
  // straight from the Clock Management Unit rather than from an Internal
  // Divider, so the Rate is whatever the Clock Tree was left at and the
  // Enable Bit in the Clock Register does not apply.
  //

  // No Feedback Delay, which is what the Device Tree asks for on this Board
  MmioWrite32 (Address + SPI_FB_CLK, 0);

  //
  // Polling is used throughout, so every Interrupt Source stays Masked.
  //
  MmioWrite32 (Address + SPI_INT_EN, 0);

  // Release the Chip Select and Drain anything Stale
  SpiAssertChipSelect (Bus, FALSE);
  SpiFlushFifo (Address);

  DEBUG ((EFI_D_INFO, "SpiDxe: SPI Bus %u Ready at 0x%lx.\n", BusNumber, Address));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SpiSetChipSelect (
  IN UINT8   BusNumber,
  IN BOOLEAN Assert)
{
  SPI_BUS_STATE *Bus = FindBus (BusNumber);

  if (Bus == NULL) {
    return EFI_NOT_FOUND;
  }

  SpiAssertChipSelect (Bus, Assert);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SpiTransfer (
  IN  UINT8   BusNumber,
  IN  UINT8  *TxBuffer  OPTIONAL,
  OUT UINT8  *RxBuffer  OPTIONAL,
  IN  UINTN   Length,
  IN  BOOLEAN HoldChipSelect)
{
  SPI_BUS_STATE *Bus = FindBus (BusNumber);
  UINT32         ChannelConfig;
  UINT32         ModeConfig;
  UINT32         Value;
  UINTN          Timeout;
  EFI_STATUS     Status = EFI_SUCCESS;

  if (Bus == NULL) {
    return EFI_NOT_FOUND;
  }

  // Verify Buffer Parameters
  if (TxBuffer == NULL && RxBuffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Length == 0) {
    return EFI_SUCCESS;
  }

  // Keep the Transfer inside one FIFO Pass
  if (Length > SPI_MAX_TRANSFER) {
    return EFI_BAD_BUFFER_SIZE;
  }

  SpiAssertChipSelect (Bus, TRUE);

  ModeConfig  = MmioRead32 (Bus->Address + SPI_MODE_CFG);
  ModeConfig &= ~(SPI_MODE_TXDMA_ON | SPI_MODE_RXDMA_ON);

  ChannelConfig  = MmioRead32 (Bus->Address + SPI_CH_CFG);
  ChannelConfig &= ~SPI_CH_TXCH_ON;

  //
  // The Receive Channel is Turned On even for a Send Only Transfer. The Packet
  // Count then Generates exactly the Clock Cycles the Transfer needs, which is
  // the Reason the Kernel Driver does the same.
  //
  ChannelConfig |= SPI_CH_RXCH_ON;

  // Program the Packet Count, which is a Byte Count at this Word Size
  Value  = MmioRead32 (Bus->Address + SPI_PACKET_CNT);
  Value &= ~SPI_PACKET_CNT_EN;
  MmioWrite32 (Bus->Address + SPI_PACKET_CNT, Value);

  MmioWrite32 (Bus->Address + SPI_PACKET_CNT, (UINT32)Length & SPI_PACKET_CNT_MASK);

  Value  = MmioRead32 (Bus->Address + SPI_PACKET_CNT);
  Value |= SPI_PACKET_CNT_EN;
  MmioWrite32 (Bus->Address + SPI_PACKET_CNT, Value);

  //
  // Fill the Transmit FIFO before Starting the Channels. This Controller only
  // Answers 32-bit Accesses, so a Byte is Written as a Word with the Byte in
  // the Low Lane rather than with a Byte Store.
  //
  //
  // A Receive Only Transfer deliberately leaves the Transmit Channel Off. The
  // Packet Count Programmed above is what Generates the Clock Cycles in that
  // Case, so there is nothing to Shift Out and no need to Push Filler Bytes.
  //
  if (TxBuffer != NULL) {
    ChannelConfig |= SPI_CH_TXCH_ON;

    for (UINTN i = 0; i < Length; i++) {
      MmioWrite32 (Bus->Address + SPI_TX_DATA, TxBuffer[i]);
    }
  }

  MmioWrite32 (Bus->Address + SPI_MODE_CFG, ModeConfig);
  MmioWrite32 (Bus->Address + SPI_CH_CFG, ChannelConfig);

  //
  // Wait for the Receive FIFO to hold the whole Transfer. Because the Receive
  // Channel is always On, this Doubles as the Completion Test for a Send Only
  // Transfer.
  //
  //
  // Time one Reasonably Long Transfer, once per Bus, and Report the Bit Rate it
  // Implies.
  //
  // The Rate cannot be Calculated from the Registers on this Controller: the Bit
  // Clock arrives from the Clock Management Unit and the Divider's Parent Rate is
  // not Readable without Walking into the Top Level Unit. Measuring an Actual
  // Transfer settles what the Divider is really Delivering, which is the only
  // Honest Basis for Changing it.
  //
  BOOLEAN Measure = (!Bus->Measured && Length >= 16);
  UINT64  Started = Measure ? GetPerformanceCounter () : 0;

  for (Timeout = SPI_TRANSFER_TIMEOUT_US; Timeout > 0; Timeout--) {
    if (SPI_RX_FIFO_LVL (MmioRead32 (Bus->Address + SPI_STATUS)) >= Length) {
      break;
    }

    MicroSecondDelay (SPI_POLL_INTERVAL_US);
  }

  if (Measure && Timeout != 0) {
    UINT64 Elapsed = GetPerformanceCounter () - Started;
    UINT64 Start   = 0;
    UINT64 End     = 0;
    UINT64 Hertz   = GetPerformanceCounterProperties (&Start, &End);

    Bus->Measured = TRUE;

    //
    // The Counter may run Downwards on some Parts, so the Difference is Taken in
    // whichever Direction is Positive.
    //
    if (Start > End) {
      Elapsed = Started - GetPerformanceCounter ();
    }

    if (Hertz && Elapsed) {
      DEBUG ((
        EFI_D_INFO,
        "SpiDxe: SPI Bus %u Shifted %u Bytes in %lu Nanoseconds, about %lu Kilobits per Second.\n",
        BusNumber,
        Length,
        DivU64x64Remainder (MultU64x32 (Elapsed, 1000000000), Hertz, NULL),
        DivU64x64Remainder (
          MultU64x32 (MultU64x32 (Length, 8), 1000),
          DivU64x64Remainder (MultU64x32 (Elapsed, 1000000), Hertz, NULL),
          NULL
          )
        ));
    }
  }


  if (Timeout == 0) {
    DEBUG ((
      EFI_D_ERROR,
      "SpiDxe: Transfer of %u Bytes on SPI Bus %u Timed out! (Status = 0x%x)\n",
      Length,
      BusNumber,
      MmioRead32 (Bus->Address + SPI_STATUS)
      ));

    Status = EFI_TIMEOUT;
    goto Exit;
  }

  //
  // Drain the Receive FIFO. The Kernel Driver Reads this Register a Byte at a
  // Time even on this Controller, but the Port Description it carries says the
  // Part only Answers 32-bit Accesses, so the Wider Access is used here. At a
  // Byte Word Size a FIFO Entry is one Byte and arrives in the Low Lane, which
  // is the same Way the Transmit Side above Pushes it.
  //
  for (UINTN i = 0; i < Length; i++) {
    Value = MmioRead32 (Bus->Address + SPI_RX_DATA);

    if (RxBuffer != NULL) {
      RxBuffer[i] = (UINT8)(Value & 0xFF);
    }

  }

Exit:
  SpiFlushFifo (Bus->Address);

  if (!HoldChipSelect || EFI_ERROR (Status)) {
    SpiAssertChipSelect (Bus, FALSE);
  }

  return Status;
}

STATIC EFI_SPI_PROTOCOL mSpi = {
  SpiInitBus,
  SpiSetChipSelect,
  SpiTransfer
};

EFI_STATUS
EFIAPI
RegisterSpi (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Locate USI Protocol
  Status = gBS->LocateProtocol (&gEfiUsiProtocolGuid, NULL, (VOID *)&mUsiProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate USI Protocol!\n"));
    return Status;
  }

  // Register SPI Protocol
  Status = gBS->InstallProtocolInterface (&ImageHandle, &gEfiSpiProtocolGuid, EFI_NATIVE_INTERFACE, &mSpi);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register SPI Protocol!\n"));
    return Status;
  }

  return EFI_SUCCESS;
}
