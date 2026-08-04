#ifndef _ACPM_H_
#define _ACPM_H_

//
// Shared Memory Description published by the Alive Core Firmware
//
// The Firmware leaves a small Table at a Fixed Offset in its Static RAM which
// Describes where the Message Queues live. Every Address in it is an Offset from
// the Base of that RAM rather than a Physical Address.
//
typedef struct {
  UINT32 Reserved[2];
  UINT32 Channels;
  UINT32 Reserved1[3];
  UINT32 ChannelCount;
} ACPM_SHARED_MEMORY;

//
// Description of a single Channel.
//
// Read this from the Alive Core's Point of View: what it calls its Receive Queue
// is the Queue this Side Sends on, and what it calls its Transmit Queue is the
// one this Side Reads. The Names are kept as the Firmware means them so that
// this Structure still matches the Kernel's, and the Swap is done once where the
// Queues are Picked up.
//
typedef struct {
  UINT32 Id;
  UINT32 Reserved[3];
  UINT32 RxRear;
  UINT32 RxFront;
  UINT32 RxBase;
  UINT32 Reserved1[3];
  UINT32 TxRear;
  UINT32 TxFront;
  UINT32 TxBase;
  UINT32 QueueLength;
  UINT32 MessageLength;
  UINT32 Reserved2[2];
  UINT32 PollCompletion;
} ACPM_CHANNEL_SHARED_MEMORY;

//
// Doorbell Register of the Mailbox towards the Alive Core. Writing the Bit that
// Matches the Channel tells the Firmware a Message is Waiting.
//
#define ACPM_MAILBOX_INTGR1             0x40

//
// The Sequence Number Sits in the first Word of every Message and is how an
// Answer is Matched to its Request. Valid Numbers run from One.
//
#define ACPM_SEQNUM_SHIFT               16
#define ACPM_SEQNUM_MASK                0x003F0000
#define ACPM_SEQNUM_MAX                 64

//
// Command and Response Layout of the Power Management Requests
//
#define ACPM_PMIC_CHANNEL_SHIFT         12
#define ACPM_PMIC_TYPE_SHIFT            8
#define ACPM_PMIC_REGISTER_SHIFT        0

#define ACPM_PMIC_FUNCTION_SHIFT        0
#define ACPM_PMIC_VALUE_SHIFT           8
#define ACPM_PMIC_MASK_SHIFT            16
#define ACPM_PMIC_RETURN_SHIFT          24

#define ACPM_PMIC_FUNCTION_READ         0
#define ACPM_PMIC_FUNCTION_WRITE        1
#define ACPM_PMIC_FUNCTION_UPDATE       2

//
// A Request and its Answer are both Four Words long
//
#define ACPM_MESSAGE_WORDS              4

//
// Timeouts
//
// The Kernel allows a Hundred Milliseconds for an Answer and half a Second for
// Room in the Queue.
//
#define ACPM_RESPONSE_TIMEOUT_US        100000
#define ACPM_QUEUE_TIMEOUT_US           500000
#define ACPM_POLL_INTERVAL_US           10

#endif /* _ACPM_H_ */
