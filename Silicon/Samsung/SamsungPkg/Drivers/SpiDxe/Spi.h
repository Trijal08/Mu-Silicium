#ifndef _SPI_H_
#define _SPI_H_

//
// SPI MMIO
//
#define SPI_MMIO_LENGTH                 0x1000

//
// Controller Registers
//
#define SPI_CH_CFG                      0x00
#define SPI_CLK_CFG                     0x04
#define SPI_MODE_CFG                    0x08
#define SPI_CS_REG                      0x0C
#define SPI_INT_EN                      0x10
#define SPI_STATUS                      0x14
#define SPI_TX_DATA                     0x18
#define SPI_RX_DATA                     0x1C
#define SPI_PACKET_CNT                  0x20
#define SPI_PENDING_CLR                 0x24
#define SPI_SWAP_CFG                    0x28
#define SPI_FB_CLK                      0x2C

//
// The USI Block that wraps the Controller sits at a Fixed Offset above it, in
// the same Page, and holds its own Reset and Clock Gating Controls.
//
#define SPI_USI_OFFSET                  0xC0
#define SPI_USI_CON                     (SPI_USI_OFFSET + 0x04)
#define SPI_USI_OPTION                  (SPI_USI_OFFSET + 0x08)

#define SPI_USI_CON_RESET               BIT0
#define SPI_USI_OPTION_CLKREQ_ON        BIT1
#define SPI_USI_OPTION_CLKSTOP_ON       BIT2

//
// Channel Configuration Register Bits
//
#define SPI_CH_HS_EN                    BIT6
#define SPI_CH_SW_RST                   BIT5
#define SPI_CH_SLAVE                    BIT4
#define SPI_CH_CPOL_L                   BIT3
#define SPI_CH_CPHA_B                   BIT2
#define SPI_CH_RXCH_ON                  BIT1
#define SPI_CH_TXCH_ON                  BIT0

//
// Clock Configuration Register Bits
//
#define SPI_ENCLK_ENABLE                BIT8
#define SPI_PSR_MASK                    0xFF

//
// Mode Configuration Register Bits
//
#define SPI_MODE_CH_TSZ_BYTE           (0 << 29)
#define SPI_MODE_CH_TSZ_MASK           (3 << 29)
#define SPI_MODE_BUS_TSZ_BYTE          (0 << 17)
#define SPI_MODE_BUS_TSZ_MASK          (3 << 17)
#define SPI_MODE_SELF_LOOPBACK          BIT3
#define SPI_MODE_RXDMA_ON               BIT2
#define SPI_MODE_TXDMA_ON               BIT1
#define SPI_MODE_4BURST                 BIT0

//
// Trailing Byte Count, which Governs how long the Controller Waits before it
// Calls a Receive Stream Finished. The Maximum is used, as the Kernel does.
//
#define SPI_MAX_TRAILCNT                0x3FF
#define SPI_TRAILCNT_OFFSET             19

//
// Pending Clear Register Bits
//
#define SPI_PND_TX_UNDERRUN_CLR         BIT4
#define SPI_PND_TX_OVERRUN_CLR          BIT3
#define SPI_PND_RX_UNDERRUN_CLR         BIT2
#define SPI_PND_RX_OVERRUN_CLR          BIT1
#define SPI_PND_TRAILING_CLR            BIT0

//
// Chip Select Register Bits
//
#define SPI_CS_NSC_CNT_2               (2 << 4)
#define SPI_CS_AUTO                     BIT1
#define SPI_CS_SIG_INACT                BIT0

//
// Status Register Bits
//
// The FIFO Level Fields moved on this Generation of the Controller, so the
// Masks below are the "V2" Ones: Receive Level in Bits 23:15 and Transmit
// Level in Bits 14:6. TX_DONE is Bit 25 here.
//
#define SPI_ST_RX_FIFO_LVL_MASK         0x00FF8000
#define SPI_ST_RX_FIFO_LVL_SHIFT        15
#define SPI_ST_TX_FIFO_LVL_MASK         0x00007FC0
#define SPI_ST_TX_FIFO_LVL_SHIFT        6
#define SPI_ST_TX_DONE                  BIT25

#define SPI_RX_FIFO_LVL(x)             (((x) & SPI_ST_RX_FIFO_LVL_MASK) >> SPI_ST_RX_FIFO_LVL_SHIFT)
#define SPI_TX_FIFO_LVL(x)             (((x) & SPI_ST_TX_FIFO_LVL_MASK) >> SPI_ST_TX_FIFO_LVL_SHIFT)

//
// Packet Count Register Bits
//
#define SPI_PACKET_CNT_EN               BIT16
#define SPI_PACKET_CNT_MASK             0xFFFF

//
// Controller Geometry
//
// The FIFO holds 64 Bytes. A Transfer is Kept below that so the whole Thing
// fits in one Pass and no Refill Loop is needed, which is what the Polled Path
// in the Kernel Driver does for short Transfers as well.
//
#define SPI_FIFO_DEPTH                  64
#define SPI_MAX_TRANSFER               (SPI_FIFO_DEPTH - 1)

//
// Timings
//
// The Chip Select Setup Delay is the "cs-clock-delay" the Device Tree asks for
// on this Board, and the Flush and Transfer Timeouts are Generous Ceilings for
// a Bus running at a few MHz.
//
#define SPI_CS_SETUP_DELAY_US           2
#define SPI_FLUSH_TIMEOUT_US            1000
#define SPI_TRANSFER_TIMEOUT_US         100000
#define SPI_POLL_INTERVAL_US            1

#endif /* _SPI_H_ */
