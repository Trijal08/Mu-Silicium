/**
  Copyright (c) 2020 Samsung Electronics Co., Ltd.
  Copyright (c) 2020 Google LLC.
  Copyright (c) 2024, Linaro Ltd.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.

  Talks to the Alive Core, the Small Always On Processor that Owns the Power
  Management Chips on this Family. Requests travel through a Ring Buffer in its
  Static RAM and are Announced with a Mailbox Doorbell. Transcribed from the
  Kernel's exynos-acpm and exynos-acpm-pmic.
**/

#include <Uefi.h>

#include <Library/AcpmLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

#include "Acpm.h"

//
// Channel State, Worked out once from the Description the Firmware Publishes
//
typedef struct {
  BOOLEAN Valid;
  UINT32  MessageLength;
  UINT32  QueueLength;

  // Queue this Side Sends on
  UINTN   TxBase;
  UINTN   TxRear;
  UINTN   TxFront;

  // Queue this Side Reads
  UINTN   RxBase;
  UINTN   RxRear;
  UINTN   RxFront;
} ACPM_CHANNEL;

STATIC ACPM_CHANNEL mChannel      = { 0 };
STATIC UINT8        mChannelIndex = 0;
STATIC UINT32       mSequence     = 0;

/**
  Picks up the Description of one Channel from the Firmware's Shared Memory.
**/
STATIC
EFI_STATUS
AcpmOpenChannel (IN UINT8 Channel)
{
  UINTN  SramBase = (UINTN)FixedPcdGet64 (PcdAcpmSramBase);
  UINTN  Shared   = SramBase + (UINTN)FixedPcdGet32 (PcdAcpmInitDataOffset);
  UINT32 ChannelCount;
  UINT32 ChannelsOffset;
  UINTN  Description;

  // Reuse the Description once it has been Read
  if (mChannel.Valid && mChannelIndex == Channel) {
    return EFI_SUCCESS;
  }

  ChannelsOffset = MmioRead32 (Shared + OFFSET_OF (ACPM_SHARED_MEMORY, Channels));
  ChannelCount   = MmioRead32 (Shared + OFFSET_OF (ACPM_SHARED_MEMORY, ChannelCount));

  //
  // A Firmware that never Started leaves this Table Blank, and an Absurd Count
  // means the Offset is being Read from the wrong Place.
  //
  if (!ChannelCount || ChannelCount > 32 || !ChannelsOffset) {
    DEBUG ((
      EFI_D_ERROR,
      "AcpmLib: The Alive Core published no usable Channel Table. (Channels at 0x%x, Count %u)\n",
      ChannelsOffset,
      ChannelCount
      ));

    return EFI_NOT_READY;
  }

  if (Channel >= ChannelCount) {
    DEBUG ((
      EFI_D_ERROR,
      "AcpmLib: Channel %u was asked for but only %u exist!\n",
      Channel,
      ChannelCount
      ));

    return EFI_NOT_FOUND;
  }

  Description = SramBase + ChannelsOffset + (Channel * sizeof (ACPM_CHANNEL_SHARED_MEMORY));

  mChannel.MessageLength = MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, MessageLength));
  mChannel.QueueLength   = MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, QueueLength));

  //
  // Here is the Swap. The Firmware's Receive Queue is the one this Side Sends
  // on, and its Transmit Queue is the one this Side Reads.
  //
  mChannel.TxBase  = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, RxBase));
  mChannel.TxRear  = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, RxRear));
  mChannel.TxFront = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, RxFront));

  mChannel.RxBase  = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, TxBase));
  mChannel.RxRear  = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, TxRear));
  mChannel.RxFront = SramBase + MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, TxFront));

  if (!mChannel.MessageLength || !mChannel.QueueLength ||
      mChannel.MessageLength < ACPM_MESSAGE_WORDS * sizeof (UINT32)) {
    DEBUG ((
      EFI_D_ERROR,
      "AcpmLib: Channel %u describes itself impossibly. (Message %u Bytes, Queue %u Deep)\n",
      Channel,
      mChannel.MessageLength,
      mChannel.QueueLength
      ));

    return EFI_NOT_READY;
  }

  mChannel.Valid = TRUE;
  mChannelIndex  = Channel;

  DEBUG ((
    EFI_D_INFO,
    "AcpmLib: Channel %u Ready. (Identity %u, Message %u Bytes, Queue %u Deep, Polled %u)\n",
    Channel,
    MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, Id)),
    mChannel.MessageLength,
    mChannel.QueueLength,
    MmioRead32 (Description + OFFSET_OF (ACPM_CHANNEL_SHARED_MEMORY, PollCompletion))
    ));

  return EFI_SUCCESS;
}

/**
  Sends one Request and Waits for the Answer that Carries the same Sequence
  Number.
**/
STATIC
EFI_STATUS
AcpmTransfer (
  IN  UINT8   Channel,
  IN  UINT32 *Request,
  OUT UINT32 *Response)
{
  EFI_STATUS Status;
  UINT32     Front;
  UINT32     Next;
  UINT32     Sequence;
  UINTN      Timeout;

  Status = AcpmOpenChannel (Channel);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Front = MmioRead32 (mChannel.TxFront);
  Next  = (Front + 1) % mChannel.QueueLength;

  //
  // Wait for Room. The Reading Side has to have Caught up far enough that at
  // least one Slot Separates the two Ends of the Ring.
  //
  for (Timeout = ACPM_QUEUE_TIMEOUT_US; Timeout > 0; Timeout -= ACPM_POLL_INTERVAL_US) {
    if (MmioRead32 (mChannel.RxFront) != Next && MmioRead32 (mChannel.TxRear) != Next) {
      break;
    }

    MicroSecondDelay (ACPM_POLL_INTERVAL_US);
  }

  if (Timeout == 0) {
    DEBUG ((EFI_D_ERROR, "AcpmLib: No Room in the Request Queue!\n"));
    return EFI_TIMEOUT;
  }

  //
  // Claim the next Sequence Number. Only one Request is ever Outstanding here,
  // so a Counter is enough where the Kernel needs a Bitmap.
  //
  mSequence = (mSequence % (ACPM_SEQNUM_MAX - 1)) + 1;
  Sequence  = mSequence;

  Request[0] = (Request[0] & ~ACPM_SEQNUM_MASK) | (Sequence << ACPM_SEQNUM_SHIFT);

  // Place the Request, then Move the Front so the Firmware can see it
  for (UINTN i = 0; i < ACPM_MESSAGE_WORDS; i++) {
    MmioWrite32 (mChannel.TxBase + (mChannel.MessageLength * Front) + (i * sizeof (UINT32)), Request[i]);
  }

  MmioWrite32 (mChannel.TxFront, Next);

  // Ring the Doorbell
  MmioWrite32 (
    (UINTN)FixedPcdGet64 (PcdAcpmMailboxBase) + ACPM_MAILBOX_INTGR1,
    1u << Channel
    );

  //
  // Drain the Answer Queue until the Reply Bearing this Sequence Number turns
  // up. Anything else in there Belongs to anyone who Spoke before us and is
  // Stepped over.
  //
  for (Timeout = ACPM_RESPONSE_TIMEOUT_US; Timeout > 0; Timeout -= ACPM_POLL_INTERVAL_US) {
    UINT32  ReplyFront = MmioRead32 (mChannel.RxFront);
    UINT32  Index      = MmioRead32 (mChannel.RxRear);
    BOOLEAN Found      = FALSE;

    while (Index != ReplyFront) {
      UINTN  Message = mChannel.RxBase + (mChannel.MessageLength * Index);
      UINT32 Word    = MmioRead32 (Message);

      if (((Word & ACPM_SEQNUM_MASK) >> ACPM_SEQNUM_SHIFT) == Sequence) {
        for (UINTN i = 0; i < ACPM_MESSAGE_WORDS; i++) {
          Response[i] = MmioRead32 (Message + (i * sizeof (UINT32)));
        }

        Found = TRUE;
      }

      Index = (Index + 1) % mChannel.QueueLength;
    }

    // Everything Seen has been Consumed, whether or not it was Ours
    if (ReplyFront != MmioRead32 (mChannel.RxRear)) {
      MmioWrite32 (mChannel.RxRear, ReplyFront);
    }

    if (Found) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (ACPM_POLL_INTERVAL_US);
  }

  DEBUG ((EFI_D_ERROR, "AcpmLib: The Alive Core did not Answer Request %u!\n", Sequence));

  return EFI_TIMEOUT;
}

/**
  Turns the Status Byte the Firmware Returns into something Meaningful. One and
  Two both mean the Register could not be Reached.
**/
STATIC
EFI_STATUS
AcpmPmicStatus (IN UINT32 Word)
{
  UINT32 Result = (Word >> ACPM_PMIC_RETURN_SHIFT) & 0xFF;

  if (Result == 0) {
    return EFI_SUCCESS;
  }

  DEBUG ((EFI_D_ERROR, "AcpmLib: The Alive Core Refused the Register Access. (Result %u)\n", Result));

  return (Result <= 2) ? EFI_ACCESS_DENIED : EFI_DEVICE_ERROR;
}

STATIC
UINT32
AcpmPmicAddress (
  IN UINT8 Type,
  IN UINT8 Register,
  IN UINT8 ChipChannel)
{
  return ((UINT32)ChipChannel << ACPM_PMIC_CHANNEL_SHIFT)
       | ((UINT32)Type        << ACPM_PMIC_TYPE_SHIFT)
       | ((UINT32)Register    << ACPM_PMIC_REGISTER_SHIFT);
}

EFI_STATUS
AcpmPmicReadRegister (
  IN  UINT8  Channel,
  IN  UINT8  Type,
  IN  UINT8  Register,
  IN  UINT8  ChipChannel,
  OUT UINT8 *Value)
{
  EFI_STATUS Status;
  UINT32     Request[ACPM_MESSAGE_WORDS]  = { 0 };
  UINT32     Response[ACPM_MESSAGE_WORDS] = { 0 };

  // Verify Value Parameter
  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Request[0] = AcpmPmicAddress (Type, Register, ChipChannel);
  Request[1] = ACPM_PMIC_FUNCTION_READ << ACPM_PMIC_FUNCTION_SHIFT;

  Status = AcpmTransfer (Channel, Request, Response);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = AcpmPmicStatus (Response[1]);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Value = (UINT8)((Response[1] >> ACPM_PMIC_VALUE_SHIFT) & 0xFF);

  return EFI_SUCCESS;
}

EFI_STATUS
AcpmPmicWriteRegister (
  IN UINT8 Channel,
  IN UINT8 Type,
  IN UINT8 Register,
  IN UINT8 ChipChannel,
  IN UINT8 Value)
{
  EFI_STATUS Status;
  UINT32     Request[ACPM_MESSAGE_WORDS]  = { 0 };
  UINT32     Response[ACPM_MESSAGE_WORDS] = { 0 };

  Request[0] = AcpmPmicAddress (Type, Register, ChipChannel);
  Request[1] = (ACPM_PMIC_FUNCTION_WRITE << ACPM_PMIC_FUNCTION_SHIFT)
             | ((UINT32)Value            << ACPM_PMIC_VALUE_SHIFT);

  Status = AcpmTransfer (Channel, Request, Response);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return AcpmPmicStatus (Response[1]);
}

EFI_STATUS
AcpmPmicUpdateRegister (
  IN UINT8 Channel,
  IN UINT8 Type,
  IN UINT8 Register,
  IN UINT8 ChipChannel,
  IN UINT8 Value,
  IN UINT8 Mask)
{
  EFI_STATUS Status;
  UINT32     Request[ACPM_MESSAGE_WORDS]  = { 0 };
  UINT32     Response[ACPM_MESSAGE_WORDS] = { 0 };

  Request[0] = AcpmPmicAddress (Type, Register, ChipChannel);
  Request[1] = (ACPM_PMIC_FUNCTION_UPDATE << ACPM_PMIC_FUNCTION_SHIFT)
             | ((UINT32)Value             << ACPM_PMIC_VALUE_SHIFT)
             | ((UINT32)Mask              << ACPM_PMIC_MASK_SHIFT);

  Status = AcpmTransfer (Channel, Request, Response);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return AcpmPmicStatus (Response[1]);
}
