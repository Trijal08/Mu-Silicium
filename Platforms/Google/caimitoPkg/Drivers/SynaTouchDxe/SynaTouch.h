#ifndef _SYNA_TOUCH_H_
#define _SYNA_TOUCH_H_

//
// Board Wiring
//
// The Touch Controller hangs off SPI Bus 20, whose Signals are on gpp4-0, 1
// and 2 with the Native Chip Select on gpp4-3. Reset is gpp1-1, Asserted Low.
// The Interrupt Line is gpn0-0, Low while a Report is Waiting; this Driver
// Polls that Level instead of taking the Interrupt.
//
#define TOUCH_SPI_BUS                   20

#define TOUCH_SPI_BANK_ID               BANK_ID_P
#define TOUCH_SPI_BANK_NUMBER           4
#define TOUCH_SPI_CLK_PIN               0
#define TOUCH_SPI_MOSI_PIN              1
#define TOUCH_SPI_MISO_PIN              2
#define TOUCH_SPI_CS_PIN                3

#define TOUCH_RESET_BANK_ID             BANK_ID_P
#define TOUCH_RESET_BANK_NUMBER         1
#define TOUCH_RESET_PIN                 1

#define TOUCH_IRQ_BANK_ID               BANK_ID_N
#define TOUCH_IRQ_BANK_NUMBER           0
#define TOUCH_IRQ_PIN                   0

//
// Functional Clock of the SPI Controller
//
// The Controller takes its Bit Clock straight from the Clock Management Unit
// rather than from an Internal Prescaler, and the Gate for it lives in the HSI0
// Unit. The Register Interface Clock is Separate and is already Running, which
// is why the Controller answers Register Accesses while Shifting nothing at all.
//
// Firmware has no Clock Framework to ask, so the Gate is Opened Directly. The
// Board Driver is the Place for it because the Clock Tree is SoC Knowledge that
// the Generic SPI Driver should not carry.
//
#define TOUCH_CMU_HSI0_ADDRESS          0x11000000
#define TOUCH_CMU_HSI0_USI2_GATE        0x2120
#define TOUCH_CMU_HSI0_USI2_GATE_BIT    BIT21

//
// The Source Mux and Divider Feeding that Gate. Only Read, for Diagnosis: an
// Open Gate with a Dead Source looks exactly like an Open Gate with a Live one.
//
#define TOUCH_CMU_HSI0_USI2_MUX         0x101C
#define TOUCH_CMU_HSI0_USI2_DIV         0x181C

//
// The Divider above hangs off the Block's Bus Clock rather than off a Dedicated
// Source, so the Bus User Mux is what decides whether anything Live reaches it.
// The Controller Option Register holds the Block Wide Power Management Bits.
//
#define TOUCH_CMU_HSI0_BUS_USER         0x0610
#define TOUCH_CMU_HSI0_BUS_MUX          0x1000
#define TOUCH_CMU_HSI0_OPTION           0x0800

//
// Divider Value for the SPI Source Clock.
//
// The Kernel never Programs this Field itself. It asks the Clock Framework for
// a Rate, "clk_set_rate(src_clk, speed * 4)", and the Framework Works out the
// Divider, the Mux above it and the Rate of the Block Feeding both. Firmware has
// no such Framework, and the Field comes out of Reset at Zero, which Divides by
// One and sends the Whole Bus Clock at the Controller. That is Tens of Megahertz
// at a Panel Rated for Ten.
//
// Rather than Guess the Bus Rate, the Divider is set to its Maximum. The
// Resulting Bit Clock is Slower than the Panel could Manage, which Costs a
// little Latency on a Touch Report and nothing else. The Field is Four Bits and
// Divides by the Value plus One.
//
#define TOUCH_CMU_HSI0_USI2_DIV_MASK    0xF
#define TOUCH_CMU_HSI0_USI2_DIV_VALUE   0xF

//
// Mode Select Register of the USI Block, which lives in the System Register
// Block rather than in the USI Block. Read Back to Confirm the Mux really did
// Land in SPI Mode. Note the Offset Coincides with the Clock Mux Offset above,
// in a Different Block.
//
#define TOUCH_SYSREG_HSI0_ADDRESS       0x11020000
#define TOUCH_SYSREG_USI20_SW_CONF      0x101C

//
// Pad Registers, Read Directly rather than through the GPIO Protocol.
//
// The Bus and Reset Pads live in the PERIC0 Pin Controller. Nothing has ever
// Proven that Block Answers at all: the Keypad that Works reaches its Pins in
// the Always On Alive Controller instead, which is a Different Block. A Pin
// Controller that Returns Zero to every Read and Drops every Write looks exactly
// like a Successful Mux followed by a Panel that will not Talk.
//
// Each Bank holds Configuration at +0x0, Data at +0x4 and Pull at +0x8, with the
// Configuration carrying Four Bits per Pin.
//
//
// Register Interface Clock of the Pin Controller.
//
// The Kernel's Pin Controller Driver wraps every Register Access in a
// clk_enable of this, so it is a Real Dependency rather than Housekeeping. It is
// Marked Critical Upstream with the Note that Disabling it Hangs the Machine, so
// it is Expected to be On already; Read to Confirm rather than Assumed.
//
#define TOUCH_CMU_PERIC0_ADDRESS        0x10800000
#define TOUCH_CMU_PERIC0_GPIO_PCLK      0x20B8
#define TOUCH_CMU_PERIC0_GPIO_PCLK_BIT  BIT21

#define TOUCH_PERIC0_ADDRESS            0x10840000
#define TOUCH_PERIC0_GPP1_OFFSET        0x20
#define TOUCH_PERIC0_GPP4_OFFSET        0x80

//
// Supply Rails
//
// Both come from the Main Power Management Chip, which hangs off the Always On
// Processor rather than off any Bus this Processor can Address, so they are
// Switched by Asking that Processor.
//
// The Enable Field is not Uniform across this Chip's Regulators: most Gate on a
// Two Bit Mode Field at Bits Seven and Six, but these two Rails use the Single
// Bit Seven with Bit Six Reserved for something else. The Kernel calls this out
// by Name for exactly these Two.
//
#define TOUCH_PMIC_CHANNEL              2
#define TOUCH_PMIC_CHIP                 0

#define TOUCH_PMIC_AVDD_REGISTER        0x2E
#define TOUCH_PMIC_VDD_REGISTER         0x43
#define TOUCH_PMIC_ENABLE_BIT           BIT7

//
// Message Framing
//
#define TCM_HEADER_SIZE                 4
#define TCM_CRC_SIZE                    2
#define TCM_HEADER_BITS                (TCM_HEADER_SIZE * 8)

#define TCM_MSG_MAX                     1024
#define TCM_OUT_MAX                     32
#define TCM_CONFIG_MAX                  256
#define TCM_PAYLOAD_MAX                 512

//
// A Bus Transaction is Capped at what fits in one Pass of the Controller FIFO,
// and a Continuation Transaction spends two of those Bytes on its own Header.
//
#define TCM_XFER_MAX                    63
#define TCM_CONT_HEADER_SIZE            2
#define TCM_CONT_DATA_MAX              (TCM_XFER_MAX - TCM_CONT_HEADER_SIZE)

#define TCM_RETRIES                     5

//
// Timings
//
#define TCM_POWER_DELAY_MS              200
#define TCM_RESET_ACTIVE_MS             2
#define TCM_RESET_DELAY_MS              50
//
// Turnaround Delay, given to the Controller between Handing it a Command and
// Reading the Answer, and between the Chunks of a Long Answer.
//
// The Kernel asks for Fifty to a Hundred Microseconds here. It gets Considerably
// more than that in Practice, because its Sleep goes through the Scheduler and
// Returns late, whereas a Firmware Delay Loop Returns on Time and gives the
// Controller Nothing Spare. Reading a Chunk before the Controller has it Ready
// does not Fail Cleanly either: instead of the Continuation Marker the Read
// brings back Rubbish, the Message is Abandoned, and the Answer is Lost.
//
// This was Masked for a long while by the Debug Build, whose Logging Donated
// Milliseconds of Accidental Turnaround at every Step. Touch Worked there and
// Failed in a Release Build, where the Logging Compiles away. The Delay is
// therefore set Well above what the Kernel Names, which Costs a Fraction of a
// Millisecond on a Touch Report and Nothing Noticeable to a Finger.
//
#define TCM_TAT_DELAY_US                250

//
// How long to Leave the Controller alone after Handing it a Command, and before
// Collecting each Chunk of a Long Answer, while Bringing the Driver up.
//
// These are Milliseconds rather than Microseconds, far above the Fifty to a
// Hundred Microseconds the Kernel Names, and the Reason is Measurable rather
// than Superstitious. The Debug Build used to Work only because its Logging Sat
// between the Send and the Read: a Sixty Character Line at the Serial Port's
// Speed takes about Five Milliseconds, and there was one such Line in the Path.
// Replacing it with a Quarter Millisecond Broke the Debug Build too, which is
// how the real Requirement became Visible.
//
// Where the Wait goes Matters as much as how long it is. Waiting after Sending a
// Command is Actively Harmful: the Controller Prepares an Answer and does not
// Hold it Indefinitely, so a Host that Dawdles before Asking finds it Gone, and
// Ten Milliseconds there Broke the Driver where Fifty Microseconds had Worked.
// The Wait belongs between Reading a Message Header and Collecting the Body
// behind it, which is exactly where the Debug Logging used to Sit.
//
// Bring Up Reads a Handful of Messages, so Five Milliseconds each Costs nothing
// Measurable. A Touch Report is Collected on the same Path but must keep up with
// a Finger, so it gets the Short Delay instead.
//
#define TCM_BRINGUP_CONTINUE_DELAY_MS   5

//
// Quiet Period Left before Issuing a Command, so the Controller has Finished
// with the Previous Exchange and is Listening again.
//
// This is the Third and last Place the old Debug Logging was Silently Helping.
// The Line Announcing the Part Name Sat between the End of Identification and
// the first Command of Setup, and the Line Reporting the Application Information
// Sat between that Command and the next. Both are around Fifty Characters, which
// is Several Milliseconds at the Serial Port's Speed, and with the Logging Gone
// the Commands Followed one another Immediately and the Controller Missed them.
//
// Waiting here is Safe in a way that Waiting after a Send is not: no Answer is
// Outstanding yet, so there is nothing for the Controller to Give up on.
//
#define TCM_INTERCOMMAND_DELAY_MS       5
#define TCM_RESP_POLL_MS                2
#define TCM_RESP_TIMEOUT_MS             3000
#define TCM_DETECT_RETRY_MS             20

//
// Commands
//
#define TCM_CMD_IDENTIFY                0x02
#define TCM_CMD_RESET                   0x04
#define TCM_CMD_GET_APPLICATION_INFO    0x20
#define TCM_CMD_GET_TOUCH_REPORT_CONFIG 0x25
#define TCM_CMD_ENTER_DEEP_SLEEP        0x2C
#define TCM_CMD_EXIT_DEEP_SLEEP         0x2D

//
// Status and Report Codes
//
#define TCM_STATUS_IDLE                 0x00
#define TCM_STATUS_OK                   0x01
#define TCM_STATUS_CONTINUED_READ       0x03
#define TCM_REPORT_IDENTIFY             0x10
#define TCM_REPORT_TOUCH                0x11

//
// Firmware Modes
//
#define TCM_MODE_APPLICATION_FIRMWARE   0x01
#define TCM_MODE_ROMBOOTLOADER          0x04
#define TCM_MODE_BOOTLOADER             0x0B

//
// Version 1 Framing Bytes
//
#define TCM_V1_MARKER                   0xA5
#define TCM_V1_PADDING                  0x5A

//
// Touch Report Descriptor Codes
//
#define TCM_TOUCH_END                       0x00
#define TCM_TOUCH_FOREACH_ACTIVE_OBJECT     0x01
#define TCM_TOUCH_FOREACH_OBJECT            0x02
#define TCM_TOUCH_FOREACH_END               0x03
#define TCM_TOUCH_PAD_TO_NEXT_BYTE          0x04
#define TCM_TOUCH_OBJECT_N_INDEX            0x06
#define TCM_TOUCH_OBJECT_N_CLASSIFICATION   0x07
#define TCM_TOUCH_OBJECT_N_X_POSITION       0x08
#define TCM_TOUCH_OBJECT_N_Y_POSITION       0x09
#define TCM_TOUCH_OBJECT_N_Z                0x0A
#define TCM_TOUCH_NUM_OF_ACTIVE_OBJECTS     0x18

//
// Object Classifications
//
#define TCM_OBJ_LIFT                    0
#define TCM_OBJ_FINGER                  1
#define TCM_OBJ_GLOVED_OBJECT           2

#define TCM_MAX_SLOTS                   10

//
// Touch Object
//
typedef struct {
  UINT8  Status;
  UINT16 X;
  UINT16 Y;
  UINT8  Z;
} TCM_OBJECT;

#endif /* _SYNA_TOUCH_H_ */
