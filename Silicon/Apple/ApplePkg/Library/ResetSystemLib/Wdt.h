#ifndef _WDT_H_
#define _WDT_H_

//
// Apple Watchdog Timer registers (apple,s8000-wdt). Offsets from the WDT base.
// The "system" watchdog forces a full SoC reset; pongoOS uses the same sequence.
//
#define WDT_CHIP_CTL   0x0C
#define WDT_SYS_TMR    0x10
#define WDT_SYS_RST    0x14
#define WDT_SYS_CTL    0x1C

//
// WDT_SYS_CTL bits
//
#define WDT_SYS_CTL_ENABLE  0x4

#endif /* _WDT_H_ */
