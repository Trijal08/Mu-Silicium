#include <Library/IoLib.h>
#include <Library/SerialPortLib.h>

#define ZUMA_UART_BASE             0x10870000
#define ZUMA_UART_UFCON            0x08
#define ZUMA_UART_UTRSTAT          0x10
#define ZUMA_UART_UFSTAT           0x18
#define ZUMA_UART_UTXH             0x20

#define ZUMA_UART_UFCON_FIFOMODE   (1 << 0)
#define ZUMA_UART_UTRSTAT_TXFE     (1 << 1)
#define ZUMA_UART_UFSTAT_TXFULL    (1 << 24)

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

// Stub implementation requirements for the EDK2 core compilation checks
UINTN EFIAPI SerialPortRead (OUT UINT8 *Buffer, IN UINTN NumberOfBytes) { return 0; }
BOOLEAN EFIAPI SerialPortPoll (VOID) { return FALSE; }
RETURN_STATUS EFIAPI SerialPortSetControl (IN UINT32 Control) { return RETURN_SUCCESS; }
RETURN_STATUS EFIAPI SerialPortGetControl (OUT UINT32 *Control) { return RETURN_SUCCESS; }
RETURN_STATUS EFIAPI SerialPortInitialize (VOID) { return RETURN_SUCCESS; }
