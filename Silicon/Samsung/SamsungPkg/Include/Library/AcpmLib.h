#ifndef _ACPM_LIB_H_
#define _ACPM_LIB_H_

//
// Access Types of the Power Management Controller's Register Space
//
#define ACPM_PMIC_TYPE_COMMON 0x00
#define ACPM_PMIC_TYPE_PMIC   0x01
#define ACPM_PMIC_TYPE_RTC    0x02
#define ACPM_PMIC_TYPE_METER  0x0A
#define ACPM_PMIC_TYPE_WLWP   0x0B
#define ACPM_PMIC_TYPE_TRIM   0x0F

/**
  This Function Reads one Register of a Power Management Chip through the Alive
  Core.

  @param[in]  Channel                      - The Alive Core Channel Serving the
                                             Power Management Chips.
  @param[in]  Type                         - The Register Space to Address.
  @param[in]  Register                     - The Register within that Space.
  @param[in]  ChipChannel                  - Which Chip on the Channel, Counted
                                             from Zero for the Main One.
  @param[out] Value                        - The Register Contents.

  @return EFI_SUCCESS                      - Successfully Read the Register.
  @return EFI_INVALID_PARAMETER            - The "Value" Parameter is NULL.
  @return EFI_NOT_READY                    - The Alive Core did not Publish a
                                             usable Channel Description.
  @return EFI_TIMEOUT                      - The Alive Core did not Answer.
  @return EFI_DEVICE_ERROR                 - The Alive Core Refused the Request.
**/
EFI_STATUS
AcpmPmicReadRegister (
  IN  UINT8  Channel,
  IN  UINT8  Type,
  IN  UINT8  Register,
  IN  UINT8  ChipChannel,
  OUT UINT8 *Value
  );

/**
  This Function Writes one Register of a Power Management Chip through the Alive
  Core.

  @param[in] Channel                       - The Alive Core Channel Serving the
                                             Power Management Chips.
  @param[in] Type                          - The Register Space to Address.
  @param[in] Register                      - The Register within that Space.
  @param[in] ChipChannel                   - Which Chip on the Channel.
  @param[in] Value                         - The Value to Write.

  @return EFI_SUCCESS                      - Successfully Wrote the Register.
  @return EFI_NOT_READY                    - The Alive Core did not Publish a
                                             usable Channel Description.
  @return EFI_TIMEOUT                      - The Alive Core did not Answer.
  @return EFI_DEVICE_ERROR                 - The Alive Core Refused the Request.
**/
EFI_STATUS
AcpmPmicWriteRegister (
  IN UINT8 Channel,
  IN UINT8 Type,
  IN UINT8 Register,
  IN UINT8 ChipChannel,
  IN UINT8 Value
  );

/**
  This Function Changes the Masked Bits of one Register of a Power Management
  Chip through the Alive Core.

  The Read, Modify and Write happen on the far Side of the Interface, so this is
  Safe against the Alive Core Firmware Touching the same Register.

  @param[in] Channel                       - The Alive Core Channel Serving the
                                             Power Management Chips.
  @param[in] Type                          - The Register Space to Address.
  @param[in] Register                      - The Register within that Space.
  @param[in] ChipChannel                   - Which Chip on the Channel.
  @param[in] Value                         - The Bits to Write.
  @param[in] Mask                          - Which Bits of the Register to
                                             Replace.

  @return EFI_SUCCESS                      - Successfully Changed the Register.
  @return EFI_NOT_READY                    - The Alive Core did not Publish a
                                             usable Channel Description.
  @return EFI_TIMEOUT                      - The Alive Core did not Answer.
  @return EFI_DEVICE_ERROR                 - The Alive Core Refused the Request.
**/
EFI_STATUS
AcpmPmicUpdateRegister (
  IN UINT8 Channel,
  IN UINT8 Type,
  IN UINT8 Register,
  IN UINT8 ChipChannel,
  IN UINT8 Value,
  IN UINT8 Mask
  );

#endif /* _ACPM_LIB_H_ */
