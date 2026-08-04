/**
  Copyright (c) 2024, Google LLC. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.

  A Synaptics TouchComm Version 1 Touch Controller on SPI, Presented as an
  Absolute Pointer. Transcribed from the Kernel's syna_tcm Driver.
**/

#include <Library/AcpmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/AbsolutePointer.h>
#include <Protocol/EFIGpio.h>
#include <Protocol/EFISpi.h>

#include "SynaTouch.h"

//
// Global Variables
//
STATIC EFI_SPI_PROTOCOL  *mSpiProtocol;
STATIC EFI_GPIO_PROTOCOL *mGpioProtocol;

//
// Driver State
//
typedef struct {
  //
  // Optional Message Trailer Features. Both are Assumed Present and then
  // Corrected once the Startup Message has been Read.
  //
  BOOLEAN HasCrc;
  BOOLEAN HasExtraRc;
  UINT16  MaxWriteSize;

  UINT16  MaxX;
  UINT16  MaxY;
  UINT16  MaxObjects;

  // Last Received Message: Header followed by the Reassembled Payload
  UINT8  *Message;
  UINT32  PayloadLength;
  UINT8   Code;

  // Raw Bus Transfer Buffers
  UINT8  *Rx;
  UINT8  *Tx;

  UINT8   Config[TCM_CONFIG_MAX];
  UINT32  ConfigLength;

  TCM_OBJECT Objects[TCM_MAX_SLOTS];
  UINT32     SlotsSeen;

  // Absolute Pointer State
  BOOLEAN                  Touching;
  UINT64                   LastX;
  UINT64                   LastY;
  BOOLEAN                  StateChanged;
} TOUCH_DEVICE;

STATIC TOUCH_DEVICE *mTouch;

STATIC EFI_ABSOLUTE_POINTER_MODE mPointerMode;

//
// TRUE while the Driver is still Interrogating the Controller. The Answers to
// those Questions come out of Firmware Tables and are Slow to Prepare, so the
// Waits are Long. Once Reports are Flowing the same Path has to keep up with a
// Finger instead, and the Waits Shorten.
//
STATIC BOOLEAN mBringUp = TRUE;

STATIC
VOID
MilliSecondDelay (IN UINTN Milliseconds)
{
  MicroSecondDelay (Milliseconds * 1000);
}

/**
  CRC-16 as the Kernel's crc_itu_t: Polynomial 0x1021, Most Significant Bit
  First, no Reflection. Used for the Optional Command and Message CRC.
**/
STATIC
UINT16
TcmCrc16 (
  IN UINT16       Crc,
  IN CONST UINT8 *Data,
  IN UINTN        Length)
{
  for (UINTN i = 0; i < Length; i++) {
    Crc ^= (UINT16)Data[i] << 8;

    for (UINT8 Bit = 0; Bit < 8; Bit++) {
      if (Crc & 0x8000) {
        Crc = (UINT16)((Crc << 1) ^ 0x1021);
      } else {
        Crc = (UINT16)(Crc << 1);
      }
    }
  }

  return Crc;
}

/**
  CRC-6 over the Leading Bits of the Buffer, Most Significant Bit First, as
  used for the TouchComm Version 2 Header. A fully Received Version 2 Header
  including its CRC Field Computes to Zero. Only used to Recognise, and then
  Reject, a Version 2 Part.
**/
STATIC
UINT8
TcmCrc6 (
  IN CONST UINT8 *Data,
  IN UINTN        Bits)
{
  STATIC CONST UINT16 Crc6Table[16] = {
       0,  268,  536,  788, 1072, 1340, 1576, 1828,
    2144, 2412, 2680, 2932, 3152, 3420, 3656, 3908
  };

  UINT16 Remainder = 0x003F << 2;
  UINT16 Value;

  for (; Bits > 8; Bits -= 8) {
    Remainder ^= *Data++;
    Remainder  = (UINT16)((Remainder << 4) ^ Crc6Table[Remainder >> 4]);
    Remainder  = (UINT16)((Remainder << 4) ^ Crc6Table[Remainder >> 4]);
  }

  if (Bits > 0) {
    Value = *Data;

    while (Bits--) {
      if (Value & 0x80) {
        Remainder ^= 0x80;
      }

      Value <<= 1;
      Remainder <<= 1;

      if (Remainder & 0x100) {
        Remainder ^= (0x03 << 2);
      }
    }
  }

  return (UINT8)((Remainder >> 2) & 0x3F);
}

STATIC
UINT16
TcmRead16 (IN CONST UINT8 *Data)
{
  return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

STATIC
UINT32
TcmRead32 (IN CONST UINT8 *Data)
{
  return (UINT32)Data[0]
       | ((UINT32)Data[1] << 8)
       | ((UINT32)Data[2] << 16)
       | ((UINT32)Data[3] << 24);
}

STATIC
EFI_STATUS
TcmSpiRead (IN UINTN Length)
{
  return mSpiProtocol->Transfer (TOUCH_SPI_BUS, NULL, mTouch->Rx, Length, FALSE);
}

STATIC
EFI_STATUS
TcmSpiWrite (IN UINTN Length)
{
  return mSpiProtocol->Transfer (TOUCH_SPI_BUS, mTouch->Tx, NULL, Length, FALSE);
}

/**
  Sends one Command Packet: the Code, a Little Endian Length and the Payload,
  plus the CRC-16 over the whole Packet when the Firmware has that Feature On.
**/
STATIC
EFI_STATUS
TcmSend (
  IN UINT8        Command,
  IN CONST UINT8 *Payload  OPTIONAL,
  IN UINT16       Length)
{
  UINTN  Total = 3 + Length;
  UINT16 Crc;

  if (Total + TCM_CRC_SIZE > TCM_OUT_MAX) {
    return EFI_INVALID_PARAMETER;
  }

  if (mTouch->MaxWriteSize && Total + TCM_CRC_SIZE > mTouch->MaxWriteSize) {
    return EFI_INVALID_PARAMETER;
  }

  mTouch->Tx[0] = Command;
  mTouch->Tx[1] = (UINT8)(Length & 0xFF);
  mTouch->Tx[2] = (UINT8)(Length >> 8);

  if (Length && Payload != NULL) {
    CopyMem (&mTouch->Tx[3], Payload, Length);
  }

  if (mTouch->HasCrc) {
    Crc = TcmCrc16 (0xFFFF, mTouch->Tx, Total);

    mTouch->Tx[Total]     = (UINT8)(Crc & 0xFF);
    mTouch->Tx[Total + 1] = (UINT8)(Crc >> 8);

    Total += TCM_CRC_SIZE;
  }

  return TcmSpiWrite (Total);
}

STATIC
EFI_STATUS
TcmSendStartupIdentify (VOID)
{
  mTouch->Tx[0] = TCM_CMD_IDENTIFY;

  return TcmSpiWrite (1);
}

/**
  Reads one Bus Transaction and Verifies the Leading Version 1 Marker, Retrying
  on Garbage.
**/
STATIC
EFI_STATUS
TcmReadPacket (IN UINTN Length)
{
  EFI_STATUS Status;

  for (UINT8 Retry = 0; Retry < TCM_RETRIES; Retry++) {
    //
    // The Kernel Waits Five to Ten Milliseconds before Trying again. The Upper
    // End is used, for the same Reason the Turnaround Delay is Generous.
    //
    if (Retry) {
      MicroSecondDelay (10000);
    }

    Status = TcmSpiRead (Length);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (mTouch->Rx[0] == TCM_V1_MARKER) {
      return EFI_SUCCESS;
    }
  }

  DEBUG ((
    EFI_D_WARN,
    "SynaTouch: No Marker in a %u Byte Read. (Leads with 0x%02x 0x%02x)\n",
    Length,
    mTouch->Rx[0],
    mTouch->Rx[1]
    ));

  return EFI_PROTOCOL_ERROR;
}

/**
  End of Message Trailer Length: one Pad Byte, plus the CRC-16 and one further
  Pad when the Firmware has those Features On.
**/
STATIC
UINTN
TcmTrailerLength (VOID)
{
  UINTN Trailer = 1;

  if (mTouch->HasCrc) {
    Trailer += TCM_CRC_SIZE;
  }

  if (mTouch->HasExtraRc) {
    Trailer += 1;
  }

  if (mTouch->HasCrc || mTouch->HasExtraRc) {
    Trailer += 1;
  }

  return Trailer;
}

/**
  Drains the Payload and Trailer of the Current Message. After a Version 1
  Header the Remaining Bytes arrive in Continuation Transactions, each Led by
  the Marker and a Continued Read Status.
**/
STATIC
EFI_STATUS
TcmReadContinued (VOID)
{
  EFI_STATUS Status;
  UINTN      Total;
  UINTN      Remaining;
  UINTN      Offset;
  UINTN      Chunk;

  if (!mTouch->PayloadLength) {
    return EFI_SUCCESS;
  }

  Total = mTouch->PayloadLength + TcmTrailerLength ();

  if (TCM_HEADER_SIZE + Total > TCM_MSG_MAX) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Remaining = Total;
  Offset    = TCM_HEADER_SIZE;

  while (Remaining) {
    Chunk = (Remaining < TCM_CONT_DATA_MAX) ? Remaining : TCM_CONT_DATA_MAX;

    //
    // A Single Remaining Byte is never worth a Transaction of its own: the
    // Device pads past the End of a Message anyway, so the Pad is Synthesised.
    //
    if (Chunk == 1) {
      mTouch->Message[Offset++] = TCM_V1_PADDING;
      Remaining--;
      continue;
    }

    //
    // Bring Up can Afford to be Patient here; a Touch Report cannot.
    //
    if (mBringUp) {
      MilliSecondDelay (TCM_BRINGUP_CONTINUE_DELAY_MS);
    } else {
      MicroSecondDelay (TCM_TAT_DELAY_US);
    }

    Status = TcmReadPacket (Chunk + TCM_CONT_HEADER_SIZE);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (mTouch->Rx[1] != TCM_STATUS_CONTINUED_READ) {
      DEBUG ((
        EFI_D_WARN,
        "SynaTouch: Bad Continuation %02x %02x after %u of %u Bytes in Chunks of %u.\n",
        mTouch->Rx[0],
        mTouch->Rx[1],
        Offset - TCM_HEADER_SIZE,
        Total,
        Chunk
        ));

      return EFI_PROTOCOL_ERROR;
    }

    CopyMem (&mTouch->Message[Offset], &mTouch->Rx[TCM_CONT_HEADER_SIZE], Chunk);

    Offset    += Chunk;
    Remaining -= Chunk;
  }

  return EFI_SUCCESS;
}

/**
  Reads the Device's Pending Message. A Header Only Read Announces the Code and
  the Payload Length, and a Message with a Payload is then Fetched through
  Continued Reads.
**/
STATIC
EFI_STATUS
TcmGetResponse (VOID)
{
  EFI_STATUS Status;
  UINTN      Total;

  Status = TcmReadPacket (TCM_HEADER_SIZE);
  if (EFI_ERROR (Status)) {
    return Status;
  }


  mTouch->Code          = mTouch->Rx[1];
  Total                 = TcmRead16 (&mTouch->Rx[2]);
  mTouch->PayloadLength = (UINT32)Total;

  if (mTouch->Code == TCM_STATUS_CONTINUED_READ) {
    return EFI_PROTOCOL_ERROR;
  }

  CopyMem (mTouch->Message, mTouch->Rx, TCM_HEADER_SIZE);

  if (!Total) {
    return EFI_SUCCESS;
  }

  if (Total > TCM_PAYLOAD_MAX) {
    DEBUG ((EFI_D_WARN, "SynaTouch: Implausible Message Length of %u Bytes!\n", Total));
    return EFI_PROTOCOL_ERROR;
  }

  if (TCM_HEADER_SIZE + Total + TcmTrailerLength () > TCM_MSG_MAX) {
    DEBUG ((EFI_D_WARN, "SynaTouch: Oversize Message of %u Bytes!\n", Total));
    return EFI_PROTOCOL_ERROR;
  }

  return TcmReadContinued ();
}

/**
  Sends a Command and Polls for the Message Answering it. Asynchronous Reports
  share the same Stream, so they are Drained while Waiting. Identify is the one
  Command Answered by a Report rather than by a Status.
**/
STATIC
EFI_STATUS
TcmExchange (
  IN UINT8        Command,
  IN CONST UINT8 *Payload  OPTIONAL,
  IN UINT16       Length)
{
  EFI_STATUS Status;
  UINTN      Waited = 0;


  //
  // Let the Controller Finish with whatever came before this. See the Header for
  // why this Wait is Safe here and Harmful a few Lines further down.
  //
  if (mBringUp) {
    MilliSecondDelay (TCM_INTERCOMMAND_DELAY_MS);
  }

  Status = TcmSend (Command, Payload, Length);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Short, and Deliberately so. The Waiting that Matters happens later, between
  // Reading a Message Header and Collecting the Body behind it. Sitting here for
  // Milliseconds instead Broke the Driver Outright: this Controller Prepares an
  // Answer and does not Hold it Indefinitely, so a Host that Dawdles before
  // Asking finds it Gone. The Build that Worked spent Fifty Microseconds here.
  //
  MicroSecondDelay (TCM_TAT_DELAY_US);

  for (;;) {
    Status = TcmGetResponse ();

    if (EFI_ERROR (Status) && Status != EFI_PROTOCOL_ERROR) {
      return Status;
    }

    if (!EFI_ERROR (Status)) {
      if (mTouch->Code == TCM_STATUS_OK) {
        return EFI_SUCCESS;
      }

      if (mTouch->Code != TCM_STATUS_IDLE) {
        if (mTouch->Code >= TCM_REPORT_IDENTIFY) {
          if (Command == TCM_CMD_IDENTIFY && mTouch->Code == TCM_REPORT_IDENTIFY) {
            return EFI_SUCCESS;
          }
        } else {
          DEBUG ((
            EFI_D_WARN,
            "SynaTouch: Command 0x%02x Failed with Status 0x%02x!\n",
            Command,
            mTouch->Code
            ));

          return EFI_DEVICE_ERROR;
        }
      }
    }

    if (Waited >= TCM_RESP_TIMEOUT_MS) {
      return EFI_TIMEOUT;
    }

    MilliSecondDelay (TCM_RESP_POLL_MS);
    Waited += TCM_RESP_POLL_MS;
  }
}

STATIC
UINT32
TcmGetBits (
  IN CONST UINT8 *Report,
  IN UINTN        ReportBits,
  IN UINTN        Offset,
  IN UINTN        Bits)
{
  UINT32 Value = 0;

  if (Offset + Bits > ReportBits) {
    return 0;
  }

  for (UINTN i = 0; i < Bits; i++) {
    UINTN Position = Offset + i;

    Value |= (UINT32)((Report[Position / 8] >> (Position % 8)) & 1) << i;
  }

  return Value;
}

/**
  Skips Descriptor Entries up to and including the next Loop End. Entity Codes
  carry a Bit Length Byte and Flow Control Codes do not.
**/
STATIC
UINTN
TcmSkipForEach (IN UINTN Index)
{
  while (Index < mTouch->ConfigLength) {
    UINT8 Code = mTouch->Config[Index++];

    switch (Code) {
      case TCM_TOUCH_FOREACH_END:
        return Index;

      case TCM_TOUCH_END:
        return Index - 1;

      case TCM_TOUCH_FOREACH_ACTIVE_OBJECT:
      case TCM_TOUCH_FOREACH_OBJECT:
      case TCM_TOUCH_PAD_TO_NEXT_BYTE:
        break;

      default:
        // Step over the Bit Length Byte
        Index++;
        break;
    }
  }

  return Index;
}

/**
  Walks the Touch Report against the Device's own Report Descriptor and Folds
  the Result into a Single Absolute Pointer Position.
**/
STATIC
VOID
TcmParseTouch (
  IN CONST UINT8 *Report,
  IN UINTN        Length)
{
  UINTN   ReportBits    = Length * 8;
  UINTN   Offset        = 0;
  UINTN   Index         = 0;
  UINTN   LoopStart     = 0;
  UINTN   ActiveObjects = 0;
  UINTN   Objects       = 0;
  UINTN   Object        = 0;
  BOOLEAN HaveActiveCount = FALSE;
  BOOLEAN ActiveOnly      = FALSE;
  UINTN   Budget          = 8 * TCM_CONFIG_MAX;
  UINT8   Code;
  UINT8   Bits;
  UINT32  Data;

  mTouch->SlotsSeen = 0;

  while (Index < mTouch->ConfigLength && Budget--) {
    Code = mTouch->Config[Index++];

    switch (Code) {
      case TCM_TOUCH_END:
        goto Done;

      case TCM_TOUCH_FOREACH_ACTIVE_OBJECT:
        Object     = 0;
        LoopStart  = Index;
        ActiveOnly = TRUE;
        break;

      case TCM_TOUCH_FOREACH_OBJECT:
        Object     = 0;
        LoopStart  = Index;
        ActiveOnly = FALSE;
        break;

      case TCM_TOUCH_FOREACH_END:
        if (Offset >= ReportBits) {
          break;
        }

        if (ActiveOnly) {
          if (HaveActiveCount) {
            Objects++;
            Object++;

            if (Objects < ActiveObjects) {
              Index = LoopStart;
            }
          } else {
            Object++;
            Index = LoopStart;
          }
        } else {
          Object++;

          if (Object < mTouch->MaxObjects) {
            Index = LoopStart;
          }
        }
        break;

      case TCM_TOUCH_PAD_TO_NEXT_BYTE:
        Offset = (Offset + 7) & ~(UINTN)7;
        break;

      case TCM_TOUCH_NUM_OF_ACTIVE_OBJECTS:
        Bits            = mTouch->Config[Index++];
        ActiveObjects   = TcmGetBits (Report, ReportBits, Offset, Bits);
        HaveActiveCount = TRUE;
        Offset         += Bits;

        if (!ActiveObjects) {
          Index = TcmSkipForEach (Index);
        }
        break;

      case TCM_TOUCH_OBJECT_N_INDEX:
        Bits    = mTouch->Config[Index++];
        Object  = TcmGetBits (Report, ReportBits, Offset, Bits);
        Offset += Bits;
        break;

      case TCM_TOUCH_OBJECT_N_CLASSIFICATION:
        Bits    = mTouch->Config[Index++];
        Data    = TcmGetBits (Report, ReportBits, Offset, Bits);
        Offset += Bits;

        if (Object < TCM_MAX_SLOTS) {
          mTouch->Objects[Object].Status = (UINT8)Data;
          mTouch->SlotsSeen |= (1u << Object);
        }
        break;

      case TCM_TOUCH_OBJECT_N_X_POSITION:
        Bits    = mTouch->Config[Index++];
        Data    = TcmGetBits (Report, ReportBits, Offset, Bits);
        Offset += Bits;

        if (Object < TCM_MAX_SLOTS) {
          mTouch->Objects[Object].X = (UINT16)Data;
        }
        break;

      case TCM_TOUCH_OBJECT_N_Y_POSITION:
        Bits    = mTouch->Config[Index++];
        Data    = TcmGetBits (Report, ReportBits, Offset, Bits);
        Offset += Bits;

        if (Object < TCM_MAX_SLOTS) {
          mTouch->Objects[Object].Y = (UINT16)Data;
        }
        break;

      case TCM_TOUCH_OBJECT_N_Z:
        Bits    = mTouch->Config[Index++];
        Data    = TcmGetBits (Report, ReportBits, Offset, Bits);
        Offset += Bits;

        if (Object < TCM_MAX_SLOTS) {
          mTouch->Objects[Object].Z = (UINT8)Data;
        }
        break;

      default:
        // Unknown Data Entity, so Step over its Bit Field
        Bits    = mTouch->Config[Index++];
        Offset += Bits;
        break;
    }
  }

Done:
  //
  // An Absolute Pointer carries a Single Position, so the Lowest Numbered
  // Object still Down wins. Anything else would need a Multi Touch Protocol
  // that Firmware does not have.
  //
  for (UINTN i = 0; i < TCM_MAX_SLOTS; i++) {
    if (!(mTouch->SlotsSeen & (1u << i))) {
      continue;
    }

    if (mTouch->Objects[i].Status == TCM_OBJ_FINGER ||
        mTouch->Objects[i].Status == TCM_OBJ_GLOVED_OBJECT) {
      mTouch->LastX        = mTouch->Objects[i].X;
      mTouch->LastY        = mTouch->Objects[i].Y;
      mTouch->Touching     = TRUE;
      mTouch->StateChanged = TRUE;

      return;
    }
  }

  // Nothing is Down any more
  if (mTouch->Touching) {
    mTouch->Touching     = FALSE;
    mTouch->StateChanged = TRUE;
  }
}

/**
  Returns TRUE when the Controller is Asserting its Interrupt Line, which it
  holds Low while a Report is Waiting to be Read.
**/
STATIC
BOOLEAN
TcmReportPending (VOID)
{
  EFI_STATUS Status;
  BOOLEAN    Level;

  Status = mGpioProtocol->GetState (
                            TOUCH_IRQ_BANK_ID,
                            TOUCH_IRQ_BANK_NUMBER,
                            TOUCH_IRQ_PIN,
                            &Level
                            );

  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  return !Level;
}

/**
  Reads and Handles whatever the Controller has Waiting.
**/
STATIC
VOID
TcmService (VOID)
{
  if (!TcmReportPending ()) {
    return;
  }

  if (EFI_ERROR (TcmGetResponse ())) {
    return;
  }

  switch (mTouch->Code) {
    case TCM_REPORT_TOUCH:
      TcmParseTouch (&mTouch->Message[TCM_HEADER_SIZE], mTouch->PayloadLength);
      break;

    case TCM_REPORT_IDENTIFY:
      DEBUG ((EFI_D_WARN, "SynaTouch: The Controller Reset itself!\n"));
      break;

    default:
      break;
  }

  //
  // Give the Controller a Moment to Release its Interrupt Line before this is
  // Called again.
  //
  // The Interface above Polls as fast as it Likes, and in a Release Build that is
  // Very fast indeed. Coming Straight back while the Line is still Low would
  // Start a Read the Controller has Nothing for, and a Read that Arrives early on
  // this Protocol does not Fail Cleanly: it Returns Rubbish where the Marker
  // should be and Desynchronises the Stream. The Kernel never meets this because
  // it is Woken by the Interrupt rather than Asking.
  //
  MicroSecondDelay (TCM_TAT_DELAY_US);
}

/**
  Powers the Touch Controller's two Supply Rails.

  Until this Runs the Panel is Dark, and a Dark Chip Clamps every Pin Driven at it
  Low through its Protection Diodes. That Clamp is Indistinguishable from a Live
  Chip Asserting an Interrupt or Holding a Reset, which is why the Bus can look
  Perfect while nothing Answers on it.

  An Update rather than a Write, so that only the Enable Bit Moves and whatever
  the Chip has in the rest of the Register is left alone.
**/
STATIC
EFI_STATUS
TouchPowerRails (VOID)
{
  EFI_STATUS Status;
  UINT8      Before = 0;
  UINT8      After  = 0;

  AcpmPmicReadRegister (
    TOUCH_PMIC_CHANNEL, ACPM_PMIC_TYPE_PMIC,
    TOUCH_PMIC_AVDD_REGISTER, TOUCH_PMIC_CHIP, &Before
    );

  // Analogue Rail
  Status = AcpmPmicUpdateRegister (
             TOUCH_PMIC_CHANNEL, ACPM_PMIC_TYPE_PMIC,
             TOUCH_PMIC_AVDD_REGISTER, TOUCH_PMIC_CHIP,
             TOUCH_PMIC_ENABLE_BIT, TOUCH_PMIC_ENABLE_BIT
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Could not Enable the Analogue Rail! (Status = %r)\n", Status));
    return Status;
  }

  // Digital Rail
  Status = AcpmPmicUpdateRegister (
             TOUCH_PMIC_CHANNEL, ACPM_PMIC_TYPE_PMIC,
             TOUCH_PMIC_VDD_REGISTER, TOUCH_PMIC_CHIP,
             TOUCH_PMIC_ENABLE_BIT, TOUCH_PMIC_ENABLE_BIT
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Could not Enable the Digital Rail! (Status = %r)\n", Status));
    return Status;
  }

  AcpmPmicReadRegister (
    TOUCH_PMIC_CHANNEL, ACPM_PMIC_TYPE_PMIC,
    TOUCH_PMIC_AVDD_REGISTER, TOUCH_PMIC_CHIP, &After
    );

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: Powered the Touch Rails. Analogue Control 0x%02x -> 0x%02x.\n",
    Before,
    After
    ));

  return EFI_SUCCESS;
}

/**
  Opens the Clock Gate that Feeds the SPI Controller's Bit Clock.

  Without this the Controller Answers every Register Access and Accepts a
  Configuration, but never Shifts a Single Bit: the Receive Level stays at Zero
  and the Transfer simply Times out.
**/
STATIC
VOID
TouchEnableSpiClock (VOID)
{
  UINT32 Value = MmioRead32 (TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_GATE);

  if (Value & TOUCH_CMU_HSI0_USI2_GATE_BIT) {
    DEBUG ((EFI_D_INFO, "SynaTouch: The SPI Clock is already Running.\n"));
    return;
  }

  MmioWrite32 (
    TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_GATE,
    Value | TOUCH_CMU_HSI0_USI2_GATE_BIT
    );

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: Opened the SPI Clock Gate. (0x%08x -> 0x%08x)\n",
    Value,
    MmioRead32 (TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_GATE)
    ));
}

/**
  Divides the SPI Source Clock down to a Rate the Panel can Follow.

  The Kernel gets this for Free by Asking its Clock Framework for a Rate and
  Letting it Work Out the Divider. Nothing here can do that, and the Field comes
  out of Reset Dividing by One, which puts the Entire Block Bus Clock on the
  Controller. See the Header for why the Maximum Divider is used rather than a
  Calculated one.
**/
STATIC
VOID
TouchSlowSpiClock (VOID)
{
  UINT32 Value = MmioRead32 (TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_DIV);
  UINT32 Wanted;

  Wanted  = Value & ~(UINT32)TOUCH_CMU_HSI0_USI2_DIV_MASK;
  Wanted |= TOUCH_CMU_HSI0_USI2_DIV_VALUE;

  if (Wanted == Value) {
    return;
  }

  MmioWrite32 (TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_DIV, Wanted);

  //
  // A Samsung Divider takes a Moment to Settle after it is Written.
  //
  MicroSecondDelay (10);

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: Set the SPI Clock Divider. (0x%08x -> 0x%08x)\n",
    Value,
    MmioRead32 (TOUCH_CMU_HSI0_ADDRESS + TOUCH_CMU_HSI0_USI2_DIV)
    ));
}




STATIC
VOID
TouchSetReset (IN BOOLEAN Asserted)
{
  //
  // Driven Straight at the Pad rather than through the GPIO Protocol, because
  // the Protocol's Data Register Write never took a One while the Panel was
  // Unpowered and Clamping the Line. Reset is Asserted Low, so Asserting it
  // Clears the Bit and Releasing it Sets the Bit.
  //
  UINTN  Data = TOUCH_PERIC0_ADDRESS + TOUCH_PERIC0_GPP1_OFFSET + 0x4;
  UINT32 Bit  = 1u << TOUCH_RESET_PIN;

  if (Asserted) {
    MmioAnd32 (Data, ~Bit);
  } else {
    MmioOr32 (Data, Bit);
  }
}

/**
  Muxes the Bus Signals and Drives the Controller through a Reset Pulse.

  The two Supply Rails, a 1.8 Volt Digital and a 3.3 Volt Analogue one, both
  come from the Main PMIC. They are left alone here: the Boot Loader has the
  Panel and its Touch Controller Powered by the Time Firmware Runs, and the
  PMIC Driver on this Platform does not Attach yet. The Power Settling Delay is
  still Honoured so the Timing matches a Cold Bring Up.
**/
/**
  Muxes one Pad and Complains if the Controller would not take it.

  Every Call in here used to Discard its Status. A Bank the Library does not know
  about Returns EFI_NOT_FOUND and Changes nothing, which looks Identical to a
  Successful Mux from the Caller's Side and leaves the Signal sitting on whatever
  Function the Boot Loader left it on.
**/
STATIC
VOID
TouchMuxPad (
  IN CONST CHAR8       *Name,
  IN EFI_GPIO_BANK_ID   BankId,
  IN UINT8              BankNumber,
  IN UINT8              Pin,
  IN EFI_GPIO_FUNCTION  Function)
{
  EFI_STATUS Status;

  Status = mGpioProtocol->SetFunction (BankId, BankNumber, Pin, Function);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Could not set the Function of %a! (Status = %r)\n", Name, Status));
    return;
  }

  Status = mGpioProtocol->SetPull (BankId, BankNumber, Pin, PULL_NONE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Could not set the Pull of %a! (Status = %r)\n", Name, Status));
  }
}

STATIC
EFI_STATUS
TouchPowerOn (VOID)
{
  //
  // Mux every Pad through the Checked Helper first, so that a Bank the GPIO
  // Library does not carry is Reported rather than Silently Ignored.
  //
  TouchMuxPad ("the SPI Clock",     TOUCH_SPI_BANK_ID,   TOUCH_SPI_BANK_NUMBER,   TOUCH_SPI_CLK_PIN,  FUNCTION_3);
  TouchMuxPad ("the SPI Output",    TOUCH_SPI_BANK_ID,   TOUCH_SPI_BANK_NUMBER,   TOUCH_SPI_MOSI_PIN, FUNCTION_3);
  TouchMuxPad ("the SPI Input",     TOUCH_SPI_BANK_ID,   TOUCH_SPI_BANK_NUMBER,   TOUCH_SPI_MISO_PIN, FUNCTION_3);
  TouchMuxPad ("the Chip Select",   TOUCH_SPI_BANK_ID,   TOUCH_SPI_BANK_NUMBER,   TOUCH_SPI_CS_PIN,   FUNCTION_3);
  TouchMuxPad ("the Interrupt",     TOUCH_IRQ_BANK_ID,   TOUCH_IRQ_BANK_NUMBER,   TOUCH_IRQ_PIN,      FUNCTION_INPUT);

  //
  // The Reset Pad is Brought up in the Order the Kernel uses, which is not the
  // Obvious one: the Data Bit is Written while the Pad is still an Input, and only
  // then is the Pad turned into an Output. samsung_gpio_direction_output does
  // exactly this, Setting the Value before Setting the Direction, so the Pad never
  // Drives an Unintended Level even Briefly. Writing the Data Bit after the Pad is
  // already an Output, which is what this Driver did before, is the Reverse of it.
  //
  // The Level Written here is the Asserted one, because the Kernel likewise asks
  // for this Line Pre Asserted and only Releases it later in the Pulse.
  //
  TouchSetReset (TRUE);
  TouchMuxPad ("the Reset",         TOUCH_RESET_BANK_ID, TOUCH_RESET_BANK_NUMBER, TOUCH_RESET_PIN,    FUNCTION_OUTPUT);

  return EFI_SUCCESS;
}

/**
  Drives the Controller through its Reset Pulse.

  This has to run AFTER the SPI Bus has been Initialised, and the Order is not
  Arbitrary. Linux gets it for free: the Bus Controller is a Separate Driver that
  Probes before any Device on the Bus, so by the Time the Touch Driver Resets
  anything the Chip Select has already been Driven Inactive. Here both Jobs
  belong to the same Driver, and the Reset used to come first, which Released the
  Controller from Reset while the Chip Select sat wherever the Boot Loader left
  it. A SPI Device that Samples its Pins as it comes out of Reset can Land in
  quite a Different Mode for that Reason.

  The Supply Settling Delay is Honoured even though the Rails are not Switched
  here, so the Timing matches a Cold Bring Up.
**/
STATIC
EFI_STATUS
TouchResetDevice (VOID)
{
  //
  // The Supplies were Switched on a Moment ago and need to Settle before the
  // Controller is Released, which is the Order the Kernel uses as well.
  //
  MilliSecondDelay (TCM_POWER_DELAY_MS);

  TouchSetReset (TRUE);
  MilliSecondDelay (TCM_RESET_ACTIVE_MS);

  TouchSetReset (FALSE);
  MilliSecondDelay (TCM_RESET_DELAY_MS);

  return EFI_SUCCESS;
}

/**
  After Reset, Sends the Raw Startup Identify Byte and Reads the Pending
  Identify Report. The first Header Read tells the Protocol Generation apart: a
  Version 1 Header Leads with the Marker, and a Version 2 Header CRC-6s to
  Zero. The Optional Trailer Features are then Sniffed and the Part is Checked
  to have come up Running Application Firmware.
**/
STATIC
EFI_STATUS
TouchDetect (VOID)
{
  EFI_STATUS   Status;
  CONST UINT8 *Trailer;
  UINT8        Tries;

  for (Tries = 0; Tries < TCM_RETRIES; Tries++) {
    Status = TcmSendStartupIdentify ();
    if (EFI_ERROR (Status)) {
      return Status;
    }

    MicroSecondDelay (TCM_TAT_DELAY_US);

    Status = TcmSpiRead (TCM_HEADER_SIZE);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (mTouch->Rx[0] == TCM_V1_MARKER) {
      break;
    }

    if (TcmCrc6 (mTouch->Rx, TCM_HEADER_BITS) == 0 &&
        mTouch->Rx[0] == TCM_REPORT_IDENTIFY) {
      DEBUG ((EFI_D_ERROR, "SynaTouch: This is a TouchComm Version 2 Device, which is not Supported!\n"));
      return EFI_UNSUPPORTED;
    }

    MilliSecondDelay (TCM_DETECT_RETRY_MS);
  }

  if (Tries == TCM_RETRIES) {
    BOOLEAN Level  = FALSE;
    BOOLEAN Pending;

    Pending = !EFI_ERROR (
                mGpioProtocol->GetState (
                                 TOUCH_IRQ_BANK_ID,
                                 TOUCH_IRQ_BANK_NUMBER,
                                 TOUCH_IRQ_PIN,
                                 &Level
                                 )
                );

    DEBUG ((
      EFI_D_ERROR,
      "SynaTouch: No TouchComm Version 1 Header. (Read 0x%02x 0x%02x 0x%02x 0x%02x)\n",
      mTouch->Rx[0],
      mTouch->Rx[1],
      mTouch->Rx[2],
      mTouch->Rx[3]
      ));

    //
    // The Interrupt Line tells the two Failure Shapes apart. Held Low means the
    // Controller is Alive and has a Message Waiting, so the Fault is in the Bus
    // Traffic. Sitting High means it never Answered the Reset at all, which
    // points at Power or at the Reset Line rather than at the Transfer.
    //
    if (Pending) {
      DEBUG ((
        EFI_D_ERROR,
        "SynaTouch: The Interrupt Line is %a.\n",
        Level ? "High, so the Controller is not Asserting it" :
                "Low, so a Message is Waiting"
        ));
    } else {
      DEBUG ((EFI_D_ERROR, "SynaTouch: The Interrupt Line could not be Read!\n"));
    }

    return EFI_NO_RESPONSE;
  }

  mTouch->Code          = mTouch->Rx[1];
  mTouch->PayloadLength = TcmRead16 (&mTouch->Rx[2]);

  if (mTouch->Code == TCM_REPORT_IDENTIFY &&
      mTouch->PayloadLength >= 24 &&
      mTouch->PayloadLength <= TCM_PAYLOAD_MAX) {
    CopyMem (mTouch->Message, mTouch->Rx, TCM_HEADER_SIZE);

    Status = TcmReadContinued ();
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SynaTouch: Startup Identify Read Failed. (Status = %r)\n", Status));
      return Status;
    }
  } else {
    //
    // No usable Startup Packet, so ask for the Identification Explicitly.
    //
    DEBUG ((EFI_D_WARN, "SynaTouch: No Startup Identify, Asking Explicitly.\n"));

    Status = TcmExchange (TCM_CMD_IDENTIFY, NULL, 0);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SynaTouch: Identify Failed. (Status = %r)\n", Status));
      return Status;
    }

    if ((mTouch->Code != TCM_STATUS_OK && mTouch->Code != TCM_REPORT_IDENTIFY) ||
        mTouch->PayloadLength < 24) {
      DEBUG ((EFI_D_ERROR, "SynaTouch: Unexpected Identify Response 0x%02x!\n", mTouch->Code));
      return EFI_PROTOCOL_ERROR;
    }
  }

  //
  // Both Trailer Features were Assumed Present for the Reads above. Over
  // Reading is Safe because the Device Pads past the End of a Message, so an
  // Assumed Trailer Byte that Reads back as Padding means the Feature is Off.
  //
  Trailer = &mTouch->Message[TCM_HEADER_SIZE + mTouch->PayloadLength];

  if (TcmRead16 (&Trailer[1]) == 0x5A5A) {
    mTouch->HasCrc = FALSE;
  }

  if (Trailer[3] == TCM_V1_PADDING) {
    mTouch->HasExtraRc = FALSE;
  }

  if (mTouch->Message[TCM_HEADER_SIZE + 1] != TCM_MODE_APPLICATION_FIRMWARE) {
    DEBUG ((
      EFI_D_ERROR,
      "SynaTouch: The Device is not Running Application Firmware. (Mode 0x%02x)\n",
      mTouch->Message[TCM_HEADER_SIZE + 1]
      ));

    return EFI_UNSUPPORTED;
  }

  mTouch->MaxWriteSize = TcmRead16 (&mTouch->Message[TCM_HEADER_SIZE + 22]);

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: TouchComm Version 1, Part '%.16a', Build %u, CRC %a, RC %a.\n",
    (CHAR8 *)&mTouch->Message[TCM_HEADER_SIZE + 2],
    TcmRead32 (&mTouch->Message[TCM_HEADER_SIZE + 18]),
    mTouch->HasCrc ? "Yes" : "No",
    mTouch->HasExtraRc ? "Yes" : "No"
    ));

  return EFI_SUCCESS;
}

/**
  Asks the Application Firmware for the Panel Geometry and for the Descriptor
  that Describes the Layout of a Touch Report.
**/
STATIC
EFI_STATUS
TouchSetupApplication (VOID)
{
  EFI_STATUS   Status;
  CONST UINT8 *Info;

  Status = TcmExchange (TCM_CMD_GET_APPLICATION_INFO, NULL, 0);

  //
  // Logged after the Exchange rather than during it, so that the Serial Port
  // cannot Lend the Controller Time the Way the old Logging did.
  //
  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: Info -> %r, Code 0x%02x, %u Bytes.\n",
    Status, mTouch->Code, mTouch->PayloadLength
    ));

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mTouch->Code != TCM_STATUS_OK || mTouch->PayloadLength < 38) {
    return EFI_PROTOCOL_ERROR;
  }

  Info = &mTouch->Message[TCM_HEADER_SIZE];

  mTouch->MaxX       = TcmRead16 (&Info[32]);
  mTouch->MaxY       = TcmRead16 (&Info[34]);
  mTouch->MaxObjects = TcmRead16 (&Info[36]);

  if (!mTouch->MaxX || !mTouch->MaxY || !mTouch->MaxObjects) {
    return EFI_DEVICE_ERROR;
  }

  if (mTouch->MaxObjects > TCM_MAX_SLOTS) {
    mTouch->MaxObjects = TCM_MAX_SLOTS;
  }

  Status = TcmExchange (TCM_CMD_GET_TOUCH_REPORT_CONFIG, NULL, 0);

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: Descriptor -> %r, Code 0x%02x, %u Bytes.\n",
    Status, mTouch->Code, mTouch->PayloadLength
    ));

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mTouch->Code != TCM_STATUS_OK ||
      !mTouch->PayloadLength ||
      mTouch->PayloadLength > TCM_CONFIG_MAX) {
    return EFI_PROTOCOL_ERROR;
  }

  CopyMem (mTouch->Config, &mTouch->Message[TCM_HEADER_SIZE], mTouch->PayloadLength);
  mTouch->ConfigLength = mTouch->PayloadLength;

  DEBUG ((
    EFI_D_INFO,
    "SynaTouch: %ux%u Panel, %u Objects, %u Byte Report Descriptor.\n",
    mTouch->MaxX,
    mTouch->MaxY,
    mTouch->MaxObjects,
    mTouch->ConfigLength
    ));

  return EFI_SUCCESS;
}

//
// Absolute Pointer Protocol
//
EFI_STATUS
EFIAPI
TouchReset (
  IN EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  IN BOOLEAN                        ExtendedVerification)
{
  mTouch->Touching     = FALSE;
  mTouch->StateChanged = FALSE;
  mTouch->LastX        = 0;
  mTouch->LastY        = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
TouchGetState (
  IN     EFI_ABSOLUTE_POINTER_PROTOCOL *This,
  IN OUT EFI_ABSOLUTE_POINTER_STATE    *State)
{
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  TcmService ();

  if (!mTouch->StateChanged) {
    return EFI_NOT_READY;
  }

  mTouch->StateChanged = FALSE;

  State->CurrentX     = mTouch->LastX;
  State->CurrentY     = mTouch->LastY;
  State->CurrentZ     = 0;
  State->ActiveButtons = mTouch->Touching ? 1 : 0;

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
TouchWaitForInput (
  IN EFI_EVENT  Event,
  IN VOID      *Context)
{
  TcmService ();

  if (mTouch->StateChanged) {
    gBS->SignalEvent (Event);
  }
}

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL mAbsolutePointer = {
  TouchReset,
  TouchGetState,
  NULL,
  &mPointerMode
};

EFI_STATUS
EFIAPI
RegisterSynaTouch (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Locate SPI Protocol
  Status = gBS->LocateProtocol (&gEfiSpiProtocolGuid, NULL, (VOID *)&mSpiProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Locate SPI Protocol!\n"));
    return Status;
  }

  // Locate GPIO Protocol
  Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID *)&mGpioProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Locate GPIO Protocol!\n"));
    return Status;
  }

  // Allocate Driver State
  mTouch = AllocateZeroPool (sizeof (TOUCH_DEVICE));
  if (mTouch == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mTouch->Message = AllocateZeroPool (TCM_MSG_MAX);
  mTouch->Rx      = AllocateZeroPool (TCM_XFER_MAX);
  mTouch->Tx      = AllocateZeroPool (TCM_OUT_MAX);

  if (mTouch->Message == NULL || mTouch->Rx == NULL || mTouch->Tx == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Fail;
  }

  //
  // Assume both Optional Trailer Features are Present until the Startup
  // Message shows otherwise.
  //
  mTouch->HasCrc     = TRUE;
  mTouch->HasExtraRc = TRUE;

  //
  // Power the Panel before anything else Touches it. The Kernel Enables these
  // Rails first as well, and Waits for them to Settle before Releasing Reset.
  //
  Status = TouchPowerRails ();
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  // Let the SPI Controller Clock, at a Rate the Panel can Follow
  TouchEnableSpiClock ();
  TouchSlowSpiClock ();

  //
  // Mux the Pads first. The Chip Select in Particular has to be under the
  // Controller's Command before the Bus is Brought up, so that Initialising the
  // Bus actually Drives it Inactive.
  //
  Status = TouchPowerOn ();
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  //
  // Mode 0 with a Chip Select this Driver Drives itself, which is what the
  // Continued Reads of this Protocol need.
  //
  Status = mSpiProtocol->InitBus (TOUCH_SPI_BUS, SPI_MODE_0, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Initialise SPI Bus %u! (Status = %r)\n", TOUCH_SPI_BUS, Status));
    goto Fail;
  }


  //
  // Only now, with the Chip Select Driven Inactive by the Bus Initialisation
  // above, is it Safe to Release the Controller from Reset.
  //
  Status = TouchResetDevice ();
  if (EFI_ERROR (Status)) {
    goto Fail;
  }


  Status = TouchDetect ();
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  Status = TouchSetupApplication ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Set up the Application Firmware! (Status = %r)\n", Status));
    goto Fail;
  }

  // Describe the Panel
  mPointerMode.AbsoluteMinX = 0;
  mPointerMode.AbsoluteMinY = 0;
  mPointerMode.AbsoluteMinZ = 0;
  mPointerMode.AbsoluteMaxX = mTouch->MaxX;
  mPointerMode.AbsoluteMaxY = mTouch->MaxY;
  mPointerMode.AbsoluteMaxZ = 0;
  mPointerMode.Attributes   = 0;

  // Create the Wait For Input Event
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  TouchWaitForInput,
                  NULL,
                  &mAbsolutePointer.WaitForInput
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Create the Input Event! (Status = %r)\n", Status));
    goto Fail;
  }

  // Register Absolute Pointer Protocol
  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gEfiAbsolutePointerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mAbsolutePointer
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SynaTouch: Failed to Register the Absolute Pointer Protocol!\n"));
    goto Fail;
  }

  //
  // Interrogation is over; Reports from here on need Speed rather than Patience.
  //
  mBringUp = FALSE;

  DEBUG ((EFI_D_INFO, "SynaTouch: Touch Screen Ready.\n"));

  return EFI_SUCCESS;

Fail:
  if (mTouch != NULL) {
    if (mTouch->Message != NULL) {
      FreePool (mTouch->Message);
    }

    if (mTouch->Rx != NULL) {
      FreePool (mTouch->Rx);
    }

    if (mTouch->Tx != NULL) {
      FreePool (mTouch->Tx);
    }

    FreePool (mTouch);
    mTouch = NULL;
  }

  return Status;
}
