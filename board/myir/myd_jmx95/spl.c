// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 MYIR
 */

#include <hang.h>
#include <init.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <asm/arch/clock.h>
#include <asm/arch/mu.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch-imx9/bbsm.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/ele_api.h>
#include <asm/mach-imx/qb.h>

DECLARE_GLOBAL_DATA_PTR;

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SD1_BOOT:
	case MMC1_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC2;
	case USB_BOOT:
	case USB2_BOOT:
		return BOOT_DEVICE_BOARD;
	case QSPI_BOOT:
		return BOOT_DEVICE_SPI;
	default:
		return BOOT_DEVICE_NONE;
	}
}

void spl_board_init(void)
{
	u32 boot_device;
	int ret;

	puts("Normal Boot\n");

	ret = ele_start_rng();
	if (ret)
		printf("Failed to start RNG: %d\n", ret);

#ifdef CONFIG_SPL_IMX_BBSM
	ret = bbsm_tamper_detect_enable();
	if (ret)
		printf("Failed to enable BBSM tamper detection: %d\n", ret);
#endif

	boot_device = spl_boot_device();
	if (boot_device == BOOT_DEVICE_BOARD) {
		ret = power_on_hsio();
		if (ret)
			printf("Failed to power on HSIO: %d\n", ret);
	}

	if (IS_ENABLED(CONFIG_SPL_IMX_QB))
		spl_qb_save();
}

void board_init_f(ulong dummy)
{
	int ret;

	memset(__bss_start, 0, __bss_end - __bss_start);

#ifdef CONFIG_SPL_RECOVER_DATA_SECTION
	if (IS_ENABLED(CONFIG_SPL_BUILD))
		spl_save_restore_data();
#endif

	timer_init();
	spl_early_init();

	ret = imx9_probe_mu();
	if (ret)
		hang();

	arch_cpu_init();
	preloader_console_init();

	debug("SOC: 0x%x\n", gd->arch.soc_rev);
	debug("LC: 0x%x\n", gd->arch.lifecycle);

	get_reset_reason(true, false);
	disable_smmuv3();
	board_init_r(NULL, 0);
}

#ifdef CONFIG_ANDROID_SUPPORT
int board_get_emmc_id(void)
{
	return 0;
}
#endif
