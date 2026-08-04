#ifndef _EFI_SPI_H_
#define _EFI_SPI_H_

//
// SPI Transfer Modes, in the usual (CPOL, CPHA) Numbering
//
typedef enum {
  SPI_MODE_0,
  SPI_MODE_1,
  SPI_MODE_2,
  SPI_MODE_3,

  SPI_MODE_NUM
} EFI_SPI_MODE;

/**
  This Function Prepares the Specified SPI Bus for Transfers.

  It Switches the Associated USI Block into SPI Mode, brings the Block out of
  Reset and Programs the Word Size, Polarity and Phase. It does not Touch the
  Pin Muxing of the Bus Signals, which is the Platform's Business.

  @param[in] BusNumber                     - The SPI Bus Number.
  @param[in] Mode                          - The SPI Transfer Mode.
  @param[in] ManualChipSelect              - TRUE if the Chip Select is Driven
                                             by SpiSetChipSelect rather than by
                                             the Controller. A Device that
                                             Answers a Read across more than
                                             one Transfer needs this.

  @return EFI_SUCCESS                      - Successfully Initialised the Bus.
  @return EFI_INVALID_PARAMETER            - The "Mode" Parameter is Invalid.
  @return EFI_NOT_FOUND                    - The Specified Bus was not Found.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_INIT_BUS) (
  IN UINT8        BusNumber,
  IN EFI_SPI_MODE Mode,
  IN BOOLEAN      ManualChipSelect
  );

/**
  This Function Asserts or Releases the Chip Select of the Specified SPI Bus.

  Only Meaningful on a Bus Initialised with ManualChipSelect set. Holding the
  Chip Select across several Transfers is Required by Devices that Abandon a
  Read if the Select Drops Part Way through it.

  @param[in] BusNumber                     - The SPI Bus Number.
  @param[in] Assert                        - TRUE to Assert the Chip Select.

  @return EFI_SUCCESS                      - Successfully Changed the Chip Select.
  @return EFI_NOT_FOUND                    - The Specified Bus was not Found.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_SET_CHIP_SELECT) (
  IN UINT8   BusNumber,
  IN BOOLEAN Assert
  );

/**
  This Function Performs one SPI Transfer on the Specified Bus.

  The Transfer is Full Duplex over the same Clock Cycles: exactly Length Bytes
  are Shifted Out of TxBuffer while Length Bytes are Shifted In to RxBuffer.
  Either Buffer may be NULL for a Transfer in one Direction only, but the
  Controller Clocks Data In regardless so that the Packet Count matches the
  Clock Cycles the Device sees.

  @param[in]  BusNumber                    - The SPI Bus Number.
  @param[in]  TxBuffer                     - The Data to Send, or NULL.
  @param[out] RxBuffer                     - The Buffer to Receive into, or NULL.
  @param[in]  Length                       - The Transfer Length in Bytes.
  @param[in]  HoldChipSelect               - TRUE to Leave the Chip Select
                                             Asserted afterwards. Ignored on a
                                             Bus without a Manual Chip Select.

  @return EFI_SUCCESS                      - Successfully Performed the Transfer.
  @return EFI_INVALID_PARAMETER            - Both Buffers are NULL.
  @return EFI_BAD_BUFFER_SIZE              - The Length exceeds the Controller FIFO.
  @return EFI_NOT_READY                    - The Bus was not Initialised.
  @return EFI_TIMEOUT                      - The Transfer did not Complete.
  @return EFI_NOT_FOUND                    - The Specified Bus was not Found.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_TRANSFER) (
  IN  UINT8   BusNumber,
  IN  UINT8  *TxBuffer  OPTIONAL,
  OUT UINT8  *RxBuffer  OPTIONAL,
  IN  UINTN   Length,
  IN  BOOLEAN HoldChipSelect
  );

//
// Define Protocol
//
typedef struct {
  EFI_SPI_INIT_BUS        InitBus;
  EFI_SPI_SET_CHIP_SELECT SetChipSelect;
  EFI_SPI_TRANSFER        Transfer;
} EFI_SPI_PROTOCOL;

#endif /* _EFI_SPI_H_ */
