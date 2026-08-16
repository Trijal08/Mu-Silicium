#include <Library/IoLib.h>
#include <Library/SerialPortLib.h>

#define ZUMA_UART_BASE             0x10870000
#define ZUMA_UART_UCON             0x04
#define ZUMA_UART_UFCON            0x08
#define ZUMA_UART_UTRSTAT          0x10
#define ZUMA_UART_UFSTAT           0x18
#define ZUMA_UART_UTXH             0x20
#define ZUMA_UART_URXH             0x24

#define ZUMA_UART_UFCON_FIFOMODE   (1 << 0)
#define ZUMA_UART_UTRSTAT_TXFE     (1 << 1)   // S3C2410_UTRSTAT_TXFE
#define ZUMA_UART_UTRSTAT_RXDR     (1 << 0)   // S3C2410_UTRSTAT_RXDR: receive data ready
#define ZUMA_UART_UFSTAT_TXFULL    (1 << 24)  // S5PV210_UFSTAT_TXFULL
#define ZUMA_UART_UFSTAT_RXFULL    (1 << 8)   // S5PV210_UFSTAT_RXFULL
#define ZUMA_UART_UFSTAT_RXMASK    (255 << 0) // S5PV210_UFSTAT_RXMASK: Rx FIFO count [7:0]

// UCON receive-mode field [1:0]: 01 = interrupt/polling (S3C2410_UCON_RXIRQMODE),
// which is what gs101_serial_drv_data's S5PV210_UCON_DEFAULT programs.
#define ZUMA_UART_UCON_RXMODE_MASK (3 << 0)
#define ZUMA_UART_UCON_RXIRQMODE   (1 << 0)

UINTN
EFIAPI
SerialPortWrite (
    IN UINT8     *Buffer,
    IN UINTN     NumberOfBytes
)
{
    UINTN i;
    for (i = 0; i < NumberOfBytes; i++) {
        // Add Carriage Return mapping for standard serial terminal outputs
        if (Buffer[i] == '\n') {
            SerialPortWrite((UINT8*)"\r", 1);
        }

        if (MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UFCON) & ZUMA_UART_UFCON_FIFOMODE) {
            while (MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UFSTAT) & ZUMA_UART_UFSTAT_TXFULL);
        } else {
            while (!(MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UTRSTAT) & ZUMA_UART_UTRSTAT_TXFE));
        }

        MmioWrite32 (ZUMA_UART_BASE + ZUMA_UART_UTXH, (UINT32)Buffer[i]);
    }
    return NumberOfBytes;
}

//
// TRUE when at least one byte is waiting in the receive path. gs101-uart uses the
// S5PV210 UFSTAT layout (mainline samsung_tty.c: gs101_serial_drv_data), so in FIFO
// mode the Rx count lives in UFSTAT[7:0] with a full bit at UFSTAT[8]; otherwise the
// UTRSTAT receive-data-ready bit applies.
//
STATIC
BOOLEAN
ZumaUartRxReady (
    VOID
)
{
    if (MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UFCON) & ZUMA_UART_UFCON_FIFOMODE) {
        UINT32 Ufstat = MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UFSTAT);
        return (Ufstat & (ZUMA_UART_UFSTAT_RXMASK | ZUMA_UART_UFSTAT_RXFULL)) != 0;
    }
    return (MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UTRSTAT) & ZUMA_UART_UTRSTAT_RXDR) != 0;
}

UINTN
EFIAPI
SerialPortRead (
    OUT UINT8    *Buffer,
    IN  UINTN     NumberOfBytes
)
{
    UINTN i;
    for (i = 0; i < NumberOfBytes; i++) {
        while (!ZumaUartRxReady ());
        Buffer[i] = (UINT8)(MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_URXH) & 0xFF);
    }
    return NumberOfBytes;
}

BOOLEAN
EFIAPI
SerialPortPoll (
    VOID
)
{
    return ZumaUartRxReady ();
}

RETURN_STATUS EFIAPI SerialPortSetControl (IN UINT32 Control) { return RETURN_SUCCESS; }
RETURN_STATUS EFIAPI SerialPortGetControl (OUT UINT32 *Control) { return RETURN_SUCCESS; }

RETURN_STATUS
EFIAPI
SerialPortInitialize (
    VOID
)
{
    // ABL already programs baud / line settings, so leave those alone. Only make sure
    // the receiver is enabled in poll mode (ABL commonly leaves the UART TX-only),
    // touching just the UCON receive-mode field so the working transmit path is intact.
    UINT32 Ucon = MmioRead32 (ZUMA_UART_BASE + ZUMA_UART_UCON);
    Ucon = (Ucon & ~ZUMA_UART_UCON_RXMODE_MASK) | ZUMA_UART_UCON_RXIRQMODE;
    MmioWrite32 (ZUMA_UART_BASE + ZUMA_UART_UCON, Ucon);
    return RETURN_SUCCESS;
}
