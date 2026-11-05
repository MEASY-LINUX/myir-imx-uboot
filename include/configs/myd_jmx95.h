/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 MYIR
 */

#ifndef __MYD_JMX95_H
#define __MYD_JMX95_H

#include <linux/sizes.h>
#include <linux/stringify.h>
#include <asm/arch/imx-regs.h>

#define CFG_SYS_INIT_RAM_ADDR		0x90000000
#define CFG_SYS_INIT_RAM_SIZE		0x200000

#define CFG_SYS_SDRAM_BASE		0x90000000
#define PHYS_SDRAM			0x90000000
#define PHYS_SDRAM_SIZE			0x70000000
#define PHYS_SDRAM_2_SIZE		0x180000000ULL

#define CFG_SYS_SECURE_SDRAM_BASE	0x8a000000
#define CFG_SYS_SECURE_SDRAM_SIZE	0x06000000

#define WDOG_BASE_ADDR			WDG3_BASE_ADDR

#ifdef CONFIG_ANDROID_SUPPORT
#include "imx95_evk_android.h"
#endif

#endif
