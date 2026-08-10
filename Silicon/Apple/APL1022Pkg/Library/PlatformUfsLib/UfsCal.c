#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/UfsCalAdapterLib.h>

#include "UfsCal.h"

//
// UFS Calibration Data for the zumapro SoC (Tensor G4).
//
// The Tables are translated from the Downstream zuma Calibration Data in
// google-modules/soc/gs/drivers/ufs/zuma/ufs-cal.h. The Downstream Entries
// carry both a UniPro Attribute (MIB) and an APB Register Offset for every
// Register, since the Kernel writes them over the APB Bus. This Driver uses
// DME Commands for the UniPro and PCS Layers, so those Entries keep the MIB,
// while the PMA Layers keep the APB Offset.
//
// The PMA Values were cross-checked against the Mainline Linux PHY Driver
// (drivers/phy/samsung/phy-gs101-ufs.c, tensor_zuma_pre_init_cfg), which
// encodes the same Registers as Indices scaled by four.
//
//
// Adapt was introduced for High Speed Gear 4. The Linux Core forces
// PA_NO_ADAPT below it (ufshcd_dme_configure_adapt), so the Adapt Type has
// to follow the negotiated Gear rather than being set for all of High Speed.
//
#define PMD_HS_BELOW_G4 (PMD_HS_G1_L1 | PMD_HS_G1_L2 | \
                         PMD_HS_G2_L1 | PMD_HS_G2_L2 | \
                         PMD_HS_G3_L1 | PMD_HS_G3_L2)
#define PMD_HS_G4       (PMD_HS_G4_L1 | PMD_HS_G4_L2)

#define PA_INITIAL_ADAPT 0x01
#define PA_NO_ADAPT      0x03

STATIC struct UfsCalParam *ufs_cal[NUM_OF_UFS_HOST];
STATIC unsigned long       ufs_cal_lock_timeout = 0xFFFFFFFF;

//
// The Reference Clock is 26 MHz. The Downstream Calibration Data selects
// 0x22 instead of 0x12 for the 38.4 MHz Boards, which zuma does not use.
//
STATIC const struct UfsCalPhyCfg init_cfg_evt1[] = {
  {0x44,   0x00,        PMD_ALL, UNIPRO_DBG_PRD,           BRD_ALL},

  {0x200,  0x40,        PMD_ALL, PHY_PCS_COMN,             BRD_ALL},
  //
  // Reference Clock Selection. The Downstream Calibration Data defaults to
  // 0x12 for 26 MHz, but the Mainline Host Driver, which is verified on this
  // Hardware, programs 0x22 for 38.4 MHz.
  //
  {0x202,  0x22,        PMD_ALL, PHY_PCS_COMN,             BRD_ALL},
  {0x12,   0x00,        PMD_ALL, PHY_PCS_RX_PRD_ROUND_OFF, BRD_ALL},
  {0xAA,   0x00,        PMD_ALL, PHY_PCS_TX_PRD_ROUND_OFF, BRD_ALL},
  {0xA9,   0x02,        PMD_ALL, PHY_PCS_TX,               BRD_ALL},
  {0xAB,   0x00,        PMD_ALL, PHY_PCS_TX_LR_PRD,        BRD_ALL},
  {0x11,   0x00,        PMD_ALL, PHY_PCS_RX,               BRD_ALL},
  {0x1B,   0x00,        PMD_ALL, PHY_PCS_RX_LR_PRD,        BRD_ALL},
  {0x2F,   0x79,        PMD_ALL, PHY_PCS_RX,               BRD_ALL},

  {0x84,   0x01,        PMD_ALL, PHY_PCS_RX,               BRD_ALL},
  {0x04,   0x01,        PMD_ALL, PHY_PCS_TX,               BRD_ALL},
  {0x25,   0xF6,        PMD_ALL, PHY_PCS_RX,               BRD_ALL},
  {0x7F,   0x00,        PMD_ALL, PHY_PCS_TX,               BRD_ALL},
  {0x200,  0x00,        PMD_ALL, PHY_PCS_COMN,             BRD_ALL},

  {0x155E, 0x00,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x3000, 0x00,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x3001, 0x01,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x4021, 0x01,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x4020, 0x01,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},

  {0x140,  0x08,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  {0x014,  0x19,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  {0x02C,  0x44,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x030,  0xC4,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x034,  0xC3,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x03C,  0x88,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x058,  0x1A,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  {0x064,  0x04,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x150,  0x88,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x19C,  0x4C,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  {0x1A0,  0x4C,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x804,  0x44,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x808,  0x44,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x80C,  0x00,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x810,  0x18,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x814,  0xC0,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x81C,  0x1C,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0xBB0,  0x8C,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x9F0,  0xD0,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0xA20,  0xFA,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0xA24,  0x60,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x8D0,  0x30,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x8E4,  0x05,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x8F4,  0x05,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x934,  0x1A,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x938,  0x12,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x93C,  0x5E,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x964,  0x2A,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x980,  0x54,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x998,  0x54,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x9CC,  0x00,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0x9D0,  0x00,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0xAAC,  0x00,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0xAB0,  0x02,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0x140,  0x0C,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x140,  0x00,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  //
  // The Downstream Calibration Data does not carry these, because the Linux
  // Host Driver writes them from its UIC Attribute Table instead
  // (exynos_ufs_config_unipro). They configure the Trailing Clock Count and
  // the Physical Adapter Debug Options, including its Line Reset Behaviour.
  //
  {0x1564, 0xFF,        PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x956A, 0x90913C1C,  PMD_ALL, UNIPRO_DBG_MIB,           BRD_ALL},
  {0x956D, 0xE01C115F,  PMD_ALL, UNIPRO_DBG_MIB,           BRD_ALL},
  //
  // The Downstream Calibration Data polls for Bit 0 here, but the Mainline
  // PHY Driver, which is verified on this Hardware, waits for Bit 3.
  //
  {0xC74,  0x08,        PMD_ALL, PHY_EMB_CAL_WAIT,         BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg post_init_cfg_evt1[] = {
  {0x15D2, 0x00,        PMD_ALL, UNIPRO_ADAPT_LENGTH,      BRD_ALL},
  {0x15D3, 0x00,        PMD_ALL, UNIPRO_ADAPT_LENGTH,      BRD_ALL},
  {0x9529, 0x01,        PMD_ALL, UNIPRO_DBG_MIB,           BRD_ALL},
  {0x15A4, 0x3E8,       PMD_ALL, UNIPRO_STD_MIB,           BRD_ALL},
  {0x9529, 0x00,        PMD_ALL, UNIPRO_DBG_MIB,           BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg calib_of_pwm[] = {
  {0x2041, 8064,        PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},
  {0x2042, 28224,       PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},
  {0x2043, 20160,       PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B0, 12000,       PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B1, 32000,       PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B2, 16000,       PMD_PWM, UNIPRO_STD_MIB,           BRD_ALL},

  {0x7888, 8064,        PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},
  {0x788C, 28224,       PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},
  {0x7890, 20160,       PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},
  {0x78B8, 12000,       PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},
  {0x78BC, 32000,       PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},
  {0x78C0, 16000,       PMD_PWM, UNIPRO_DBG_APB,           BRD_ALL},

  {0, 0, 0, 0, 0},
};

//
// The Physical Layer Configuration for a Power Mode Change. The Downstream
// Calibration Data leaves this to the Linux PHY Driver, whose gs101 Tables
// zuma reuses (tensor_gs101_pre_pwr_hs_config, tensor_gs101_post_pwr_hs_config).
// Without them the Link reports the new Gear but cannot carry Traffic.
//
STATIC const struct UfsCalPhyCfg post_calib_of_pwm[] = {
  {0x020,  0x60,        PMD_PWM, PHY_PMA_COMN,             BRD_ALL},
  {0x888,  0x08,        PMD_PWM, PHY_PMA_TRSV,             BRD_ALL},
  {0x918,  0x01,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg calib_of_hs_rate_a[] = {
  {0x15D4, PA_NO_ADAPT,      PMD_HS_BELOW_G4, UNIPRO_STD_MIB,      BRD_ALL},
  {0x15D4, PA_INITIAL_ADAPT, PMD_HS_G4,       UNIPRO_STD_MIB,      BRD_ALL},

  {0x2041, 8064,        PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x2042, 28224,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x2043, 20160,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B0, 12000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B1, 32000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B2, 16000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},

  {0x7888, 8064,        PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x788C, 28224,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x7890, 20160,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78B8, 12000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78BC, 32000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78C0, 16000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},

  {0xDA4,  0x11,        PMD_HS,  PHY_PMA_TRSV,             BRD_ALL},
  {0x918,  0x03,        PMD_HS,  PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg post_calib_of_hs_rate_a[] = {
  {0x918,  0x01,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg calib_of_hs_rate_b[] = {
  {0x15D4, PA_NO_ADAPT,      PMD_HS_BELOW_G4, UNIPRO_STD_MIB,      BRD_ALL},
  {0x15D4, PA_INITIAL_ADAPT, PMD_HS_G4,       UNIPRO_STD_MIB,      BRD_ALL},

  {0x2041, 8064,        PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x2042, 28224,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x2043, 20160,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B0, 12000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B1, 32000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},
  {0x15B2, 16000,       PMD_HS,  UNIPRO_STD_MIB,           BRD_ALL},

  {0x7888, 8064,        PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x788C, 28224,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x7890, 20160,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78B8, 12000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78BC, 32000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},
  {0x78C0, 16000,       PMD_HS,  UNIPRO_DBG_APB,           BRD_ALL},

  {0xDA4,  0x11,        PMD_HS,  PHY_PMA_TRSV,             BRD_ALL},
  {0x918,  0x03,        PMD_HS,  PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg post_calib_of_hs_rate_b[] = {
  {0x918,  0x01,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg post_h8_enter[] = {
  {0x9F4,  0x08,        PMD_ALL, PHY_PMA_TRSV_SQ,          BRD_ALL},
  {0xA00,  0x3A,        PMD_ALL, PHY_PMA_TRSV_SQ,          BRD_ALL},
  {0x000,  0x51,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},

  {0xB64,  0x30,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0xB64,  0x33,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg pre_h8_exit[] = {
  {0x000,  0x11,        PMD_ALL, PHY_PMA_COMN,             BRD_ALL},
  {0x000,  0x0A,        PMD_ALL, COMMON_WAIT,              BRD_ALL},
  {0x9F4,  0x00,        PMD_ALL, PHY_PMA_TRSV_SQ,          BRD_ALL},
  {0xA00,  0x30,        PMD_ALL, PHY_PMA_TRSV_SQ,          BRD_ALL},

  {0xB64,  0x32,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},
  {0xB64,  0x22,        PMD_ALL, PHY_PMA_TRSV,             BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC const struct UfsCalPhyCfg lane1_sq_off[] = {
  {0x9F4,  0x08,        PMD_ALL, PHY_PMA_TRSV_LANE1_SQ_OFF, BRD_ALL},
  {0xA00,  0x3A,        PMD_ALL, PHY_PMA_TRSV_LANE1_SQ_OFF, BRD_ALL},

  {0, 0, 0, 0, 0},
};

STATIC UfsCalError ufs_cal_wait_pll_lock (void *hba, UINT32 addr, UINT32 mask)
{
  for (INTN i = 0; i < 100; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_COMN_ADDR (addr)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_ERROR, "UFS CAL: PLL lock timeout\n"));
  return UFS_CAL_ERROR;
}

STATIC UfsCalError ufs_cal_wait_cdr_lock (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  for (INTN i = 0; i < 1000; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;

    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_ERROR, "UFS CDR lock timeout! lane %d\n", lane));
  return UFS_CAL_ERROR;
}

STATIC UfsCalError ufs30_cal_wait_cdr_lock (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  for (INTN i = 0; i < 100; i++) {
    MicroSecondDelay (40);

    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;

    MicroSecondDelay (1);

    ufs_lld_pma_write (hba, 0x10, PHY_PMA_TRSV_ADDR (0x888, lane));
    ufs_lld_pma_write (hba, 0x18, PHY_PMA_TRSV_ADDR (0x888, lane));
  }

  DEBUG ((EFI_D_ERROR, "UFS 3p0 CDR lock timeout! lane %d\n", lane));
  return UFS_CAL_ERROR;
}

//
// The Receiver reports Calibration Completion in one of two Places: the
// Downstream zuma Calibration Data polls TRSV 0x31D (APB 0xC74) while the
// gs101 Path polls TRSV 0x338 (APB 0xCE0). Accept either, matching the
// Mainline PHY Driver.
//
#define ZUMAPRO_RX_CAL_DONE_ALT 0xCE0

STATIC UfsCalError ufs30_cal_done_wait (void *hba, UINT32 addr, UINT32 mask, int lane)
{
  UINT32 Primary   = 0;
  UINT32 Alternate = 0;

  for (INTN i = 0; i < 5000; i++) {
    Primary   = ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane));
    Alternate = ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (ZUMAPRO_RX_CAL_DONE_ALT, lane));

    if (((Primary & mask) == mask) || ((Alternate & mask) == mask))
      return UFS_CAL_NO_ERROR;

    MicroSecondDelay (40);
  }

  DEBUG ((EFI_D_ERROR, "UFS CAL: RX calibration timeout! lane %d (0x%x = 0x%08x, 0x%x = 0x%08x, mask 0x%x)\n",
          lane, addr, Primary, ZUMAPRO_RX_CAL_DONE_ALT, Alternate, mask));
  return UFS_CAL_ERROR;
}

STATIC UfsCalError MatchModeByFlag (struct UicPwrMode *Pmd, UINT32 Flag)
{
  UINT8 m = Pmd->Mode;
  UINT8 g = Pmd->Gear;

  if (Flag == PMD_ALL)
    return UFS_CAL_NO_ERROR;

  if (IS_PWR_MODE_HS (m) && Flag == PMD_HS)
    return UFS_CAL_NO_ERROR;
  if (IS_PWR_MODE_PWM (m) && Flag == PMD_PWM)
    return UFS_CAL_NO_ERROR;

  if (IS_PWR_MODE_HS (m)) {
    if (g == 1 && (Flag & (PMD_HS_G1_L1 | PMD_HS_G1_L2))) return UFS_CAL_NO_ERROR;
    if (g == 2 && (Flag & (PMD_HS_G2_L1 | PMD_HS_G2_L2))) return UFS_CAL_NO_ERROR;
    if (g == 3 && (Flag & (PMD_HS_G3_L1 | PMD_HS_G3_L2))) return UFS_CAL_NO_ERROR;
    if (g == 4 && (Flag & (PMD_HS_G4_L1 | PMD_HS_G4_L2))) return UFS_CAL_NO_ERROR;
  }

  if (IS_PWR_MODE_PWM (m)) {
    if (g == 1 && (Flag & (PMD_PWM_G1_L1 | PMD_PWM_G1_L2))) return UFS_CAL_NO_ERROR;
    if (g == 2 && (Flag & (PMD_PWM_G2_L1 | PMD_PWM_G2_L2))) return UFS_CAL_NO_ERROR;
    if (g == 3 && (Flag & (PMD_PWM_G3_L1 | PMD_PWM_G3_L2))) return UFS_CAL_NO_ERROR;
    if (g == 4 && (Flag & (PMD_PWM_G4_L1 | PMD_PWM_G4_L2))) return UFS_CAL_NO_ERROR;
    if (g == 5 && (Flag & (PMD_PWM_G5_L1 | PMD_PWM_G5_L2))) return UFS_CAL_NO_ERROR;
  }

  return UFS_CAL_ERROR;
}

STATIC UfsCalError ufs_cal_config_uic (
  struct UfsCalParam        *p,
  const struct UfsCalPhyCfg *cfg,
  struct UicPwrMode         *Pmd)
{
  void  *hba      = p->Host;
  UINT8  Lane;
  UINT8  NumLanes = p->AvailableLane;

  if (!cfg)
    return UFS_CAL_INV_ARG;

  for_each_phy_cfg (cfg) {
    if (!(cfg->Board & p->Board))
      continue;

    for (Lane = 0; Lane < NumLanes; Lane++) {
      if (Pmd && (MatchModeByFlag (Pmd, cfg->Flag) == UFS_CAL_ERROR))
        continue;

      switch (cfg->Layer) {
      case UNIPRO_STD_MIB:
      case UNIPRO_DBG_MIB:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), cfg->Value);
        break;

      case UNIPRO_DBG_PRD:
        if (Lane == 0) {
          if (p->Table == HOST_EMBD)
            ufs_lld_unipro_write (hba, (UINT32)UNIPRO18_MCLK_PERIOD (p), cfg->Address);
          else
            ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), UNIPRO_MCLK_PERIOD (p));
        }
        break;

      case UNIPRO_DBG_APB:
        if (Lane == 0)
          ufs_lld_unipro_write (hba, cfg->Value, cfg->Address);
        break;

      case UNIPRO_ADAPT_LENGTH:
        if (Lane == 0) {
          UINT32 Val = 0;
          ufs_lld_dme_get (hba, UIC_ARG_MIB (cfg->Address), &Val);
          if (Val & 0x80) {
            if ((Val & 0x7F) < 2)
              ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), 0x82);
          } else {
            if (((Val + 1) % 4) != 0) {
              do { Val++; } while (((Val + 1) % 4) != 0);
              ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), Val);
            }
          }
        }
        break;

      case PHY_PCS_COMN:
        if (Lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->Address), cfg->Value);
        break;

      case PHY_PCS_RXTX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane), cfg->Value);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane), cfg->Value);
        break;

      case PHY_PCS_RX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane), cfg->Value);
        break;

      case PHY_PCS_TX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane), cfg->Value);
        break;

      case PHY_PCS_RX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;

      case PHY_PCS_TX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;

      case PHY_PCS_RX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, RX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;

      case PHY_PCS_TX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address, TX_LANE_0 + Lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;

      case PHY_PCS_RX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address,   RX_LANE_0 + Lane),
                         (PCS_RX_LINE_RESET_DETECT_PERIOD (p) >> 16) & 0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+1, RX_LANE_0 + Lane),
                         (PCS_RX_LINE_RESET_DETECT_PERIOD (p) >>  8) & 0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+2, RX_LANE_0 + Lane),
                         (PCS_RX_LINE_RESET_DETECT_PERIOD (p) >>  0) & 0xFF);
        break;

      case PHY_PCS_TX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address,   TX_LANE_0 + Lane),
                         (PCS_TX_LINE_RESET_PERIOD (p) >> 16) & 0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+1, TX_LANE_0 + Lane),
                         (PCS_TX_LINE_RESET_PERIOD (p) >>  8) & 0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->Address+2, TX_LANE_0 + Lane),
                         (PCS_TX_LINE_RESET_PERIOD (p) >>  0) & 0xFF);
        break;

      case PHY_PMA_COMN:
        if (Lane == 0)
          ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_COMN_ADDR (cfg->Address));
        break;

      case PHY_PMA_TRSV:
        ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        break;

      case PHY_PMA_TRSV_LANE1_SQ_OFF:
        if (Lane == 1) {
          if (p->ConnectedRxLane < p->AvailableLane)
            ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        }
        break;

      case PHY_PMA_TRSV_SQ:
        if (Lane < p->ConnectedRxLane)
          ufs_lld_pma_write (hba, cfg->Value, PHY_PMA_TRSV_ADDR (cfg->Address, Lane));
        break;

      case PHY_PLL_WAIT:
        if (Lane == 0) {
          if (ufs_cal_wait_pll_lock (hba, cfg->Address, cfg->Value) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;

      case PHY_CDR_WAIT:
        if (Lane < p->ActiveRxLane) {
          if (ufs_cal_wait_cdr_lock (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;

      case PHY_EMB_CDR_WAIT:
        if (Lane < p->ActiveRxLane) {
          if (ufs30_cal_wait_cdr_lock (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;

      case COMMON_WAIT:
        if (Lane == 0)
          MicroSecondDelay (cfg->Value);
        break;

      case PHY_EMB_CAL_WAIT:
        if (ufs30_cal_done_wait (hba, cfg->Address, cfg->Value, Lane) == UFS_CAL_ERROR)
          return UFS_CAL_TIMEOUT;
        break;

      default:
        break;
      }
    }
  }

  return UFS_CAL_NO_ERROR;
}

/*
 * This is a recommendation from Samsung UFS device vendor.
 *
 * Activate time: host < device
 * Hibern time: host > device
 */
STATIC VOID ufs_cal_calib_hibern8_values (void *hba)
{
  UINT32 hw_cap_min_tactivate;
  UINT32 peer_rx_min_actv_time_cap;
  UINT32 max_rx_hibern8_time_cap;

  ufs_lld_dme_get (hba, UIC_ARG_MIB_SEL (0x8F, RX_LANE_0), &hw_cap_min_tactivate);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A8), &peer_rx_min_actv_time_cap);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A7), &max_rx_hibern8_time_cap);

  if (peer_rx_min_actv_time_cap >= hw_cap_min_tactivate)
    ufs_lld_dme_peer_set (hba, UIC_ARG_MIB (0x15A8), peer_rx_min_actv_time_cap + 1);
  ufs_lld_dme_set (hba, UIC_ARG_MIB (0x15A7), max_rx_hibern8_time_cap + 1);
}

UfsCalError ufs_cal_post_h8_enter (struct UfsCalParam *p)
{
  return ufs_cal_config_uic (p, post_h8_enter, p->Pmd);
}

UfsCalError ufs_cal_pre_h8_exit (struct UfsCalParam *p)
{
  return ufs_cal_config_uic (p, pre_h8_exit, p->Pmd);
}

UfsCalError UfsCalPrePmc (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;

  if ((p->Pmd->Mode == SLOW_MODE) || (p->Pmd->Mode == SLOWAUTO_MODE))
    cfg = calib_of_pwm;
  else if (p->Pmd->HsSeries == PA_HS_MODE_B)
    cfg = calib_of_hs_rate_b;
  else if (p->Pmd->HsSeries == PA_HS_MODE_A)
    cfg = calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError UfsCalPostPmc (struct UfsCalParam *p)
{
  const struct UfsCalPhyCfg *cfg;

  if ((p->Pmd->Mode == SLOWAUTO_MODE) || (p->Pmd->Mode == SLOW_MODE))
    cfg = post_calib_of_pwm;
  else if (p->Pmd->HsSeries == PA_HS_MODE_B)
    cfg = post_calib_of_hs_rate_b;
  else if (p->Pmd->HsSeries == PA_HS_MODE_A)
    cfg = post_calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->Pmd);
}

UfsCalError UfsCalPostLink (struct UfsCalParam *p)
{
  UfsCalError ret;

  ufs_cal_calib_hibern8_values (p->Host);

  //
  // Unlike the older Exynos SoCs, the zuma Calibration Data uses one
  // Post Link Table for every Gear.
  //
  ret = ufs_cal_config_uic (p, post_init_cfg_evt1, NULL);

  if (ret == UFS_CAL_NO_ERROR) {
    if ((p->AvailableLane == 2) && (p->ConnectedRxLane == 1))
      ret = ufs_cal_config_uic (p, lane1_sq_off, NULL);
  }

  return ret;
}

UfsCalError UfsCalPreLink (struct UfsCalParam *p)
{
  //
  // Only the EVT1 Calibration Data is carried here, which covers Production
  // Silicon. An EVT0 Part would need the separate init_cfg_evt0 Table from
  // the Downstream Calibration Data.
  //
  return ufs_cal_config_uic (p, init_cfg_evt1, NULL);
}

UINT8 UfsCalGetTargetBoard (VOID)
{
  //
  // The Device Tree sets "brd-for-cal" to 1. Every Entry above is tagged
  // BRD_ALL, so the exact Board only matters for future Tables.
  //
  return BRD_SMDK;
}

UfsCalError UfsCalInit (struct UfsCalParam *p)
{
  ufs_cal[0]           = p;
  ufs_cal_lock_timeout = 0xFFFFFFFF;

  return UFS_CAL_NO_ERROR;
}
