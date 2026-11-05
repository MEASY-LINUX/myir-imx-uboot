// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 MYIR
 */

#include <efi_loader.h>
#include <env.h>
#include <fdt_support.h>
#include <i2c_eeprom.h>
#include <init.h>
#include <net.h>
#include <scmi_agent.h>
#include <timestamp.h>
#include <u-boot/crc.h>
#include <usb.h>
#include <version.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/gpio.h>
#include <dm/uclass.h>
#include <dm/uclass-internal.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include "../../nxp/common/tcpc.h"
#include "../../../dts/upstream/src/arm64/freescale/imx95-power.h"

#define MYIR_EEPROM_CRC_SEED	0x12345678U
#define MYIR_CRC32C_POLY	0x82f63b78U

struct myir_eeprom {
	u32 crc32;
	char part_number[48];
	char serial_number[48];
	char mac0[32];
	char mac1[32];
};

extern int board_fix_fdt_fuse(void *fdt);

#if CONFIG_IS_ENABLED(EFI_HAVE_CAPSULE_SUPPORT)
#define IMX_BOOT_IMAGE_GUID \
	EFI_GUID(0x2c4db6b3, 0x0b15, 0x4a36, 0xbe, 0xae, \
		 0x1e, 0xa1, 0x35, 0x46, 0x4f, 0x5b)

struct efi_fw_image fw_images[] = {
	{
		.image_type_id = IMX_BOOT_IMAGE_GUID,
		.fw_name = u"MYD-JMX95-RAW",
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "mmc 0=flash-bin raw 0 0x2000 mmcpart 1",
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};
#endif

#ifdef CONFIG_USB_TCPC
static struct tcpc_port port;
static struct tcpc_port_config port_config = {
	.i2c_bus = 2,
	.addr = 0x52,
	.port_type = TYPEC_PORT_DRP,
	.disable_pd = true,
};

static ulong tca_base;

static void tca_mux_select(enum typec_cc_polarity polarity)
{
	if (!tca_base)
		return;

	clrbits_le32(tca_base + 0x10, 0x3);
	setbits_le32(tca_base + 0x18, BIT(3));
	udelay(1);

	if (polarity == TYPEC_POLARITY_CC1)
		clrbits_le32(tca_base + 0x18, BIT(2));
	else
		setbits_le32(tca_base + 0x18, BIT(2));

	udelay(1);
	clrbits_le32(tca_base + 0x18, BIT(3));
}

static void setup_typec(void)
{
	int ret;

	tca_base = USB1_BASE_ADDR + 0xfc000;
	ret = tcpc_init(&port, port_config, &tca_mux_select);
	if (ret)
		printf("%s: TCPC init failed: %d\n", __func__, ret);
}
#endif

static int imx9_scmi_power_domain_enable(u32 domain, bool enable)
{
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_name(UCLASS_CLK, "protocol@14", &dev);
	if (ret)
		return ret;

	return scmi_pwd_state_set(dev, 0, domain, enable ? 0 : BIT(30));
}

int board_usb_init(int index, enum usb_init_type init)
{
	if (index != 0)
		return 0;

#ifdef CONFIG_USB_TCPC
	if (init == USB_INIT_DEVICE)
		return tcpc_setup_ufp_mode(&port);
	if (init == USB_INIT_HOST)
		return tcpc_setup_dfp_mode(&port);
#endif

	return 0;
}

int board_usb_cleanup(int index, enum usb_init_type init)
{
#ifdef CONFIG_USB_TCPC
	if (index == 0 && init == USB_INIT_HOST)
		return tcpc_disable_src_vbus(&port);
#endif

	return 0;
}

static void pulse_reset(const char *gpio_name, const char *label,
			bool active_low)
{
	struct gpio_desc desc;
	ulong flags = GPIOD_IS_OUT;
	int ret;

	ret = dm_gpio_lookup_name(gpio_name, &desc);
	if (ret) {
		printf("%s: lookup %s failed: %d\n", __func__, gpio_name, ret);
		return;
	}

	ret = dm_gpio_request(&desc, label);
	if (ret) {
		printf("%s: request %s failed: %d\n", __func__, label, ret);
		return;
	}

	if (active_low)
		flags |= GPIOD_ACTIVE_LOW;

	dm_gpio_set_dir_flags(&desc, flags);
	dm_gpio_set_value(&desc, 1);
	mdelay(10);
	dm_gpio_set_value(&desc, 0);
	mdelay(80);
}

static void netc_init(void)
{
	int ret;

	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, false);
	if (ret)
		debug("NETC power down failed: %d\n", ret);
	mdelay(10);

	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, true);
	if (ret) {
		printf("NETC power up failed: %d\n", ret);
		return;
	}

	pulse_reset("gpio@20_6", "ENET1_RST_B", true);
	pulse_reset("GPIO2_30", "ENET2_RST_B", true);
}

static void myir_board_init(void)
{
	pulse_reset("gpio@20_1", "USB_HUB_RST", false);
}

static int myir_set_data_from_eeprom(void)
{
	struct myir_eeprom eeprom;
	struct udevice *dev;
	u32 table[256];
	u8 mac[ARP_HLEN];
	u32 crc;
	int ret;

	ret = uclass_first_device_err(UCLASS_I2C_EEPROM, &dev);
	if (ret)
		return ret;

	ret = i2c_eeprom_read(dev, 0, (u8 *)&eeprom, sizeof(eeprom));
	if (ret)
		return ret;

	crc32c_init(table, MYIR_CRC32C_POLY);
	crc = crc32c_cal(MYIR_EEPROM_CRC_SEED,
			 (const char *)&eeprom.part_number,
			 sizeof(eeprom) - sizeof(eeprom.crc32), table);
	if (eeprom.crc32 != crc) {
		printf("MYIR EEPROM CRC mismatch: stored=%08x calculated=%08x\n",
		       eeprom.crc32, crc);
		return -EBADMSG;
	}

	eeprom.part_number[sizeof(eeprom.part_number) - 1] = '\0';
	eeprom.serial_number[sizeof(eeprom.serial_number) - 1] = '\0';
	eeprom.mac0[sizeof(eeprom.mac0) - 1] = '\0';
	eeprom.mac1[sizeof(eeprom.mac1) - 1] = '\0';

	printf("MYIR PN: %s\n", eeprom.part_number);
	printf("MYIR SN: %s\n", eeprom.serial_number);

	string_to_enetaddr(eeprom.mac0, mac);
	if (is_valid_ethaddr(mac))
		eth_env_set_enetaddr("ethaddr", mac);

	string_to_enetaddr(eeprom.mac1, mac);
	if (is_valid_ethaddr(mac))
		eth_env_set_enetaddr("eth1addr", mac);

	return 0;
}

int board_init(void)
{
	int ret;

	ret = imx9_scmi_power_domain_enable(IMX95_PD_HSIO_TOP, true);
	if (ret) {
		printf("HSIO power up failed: %d\n", ret);
		return ret;
	}

	imx9_scmi_power_domain_enable(IMX95_PD_DISPLAY, false);
	imx9_scmi_power_domain_enable(IMX95_PD_CAMERA, false);

#ifdef CONFIG_USB_TCPC
	setup_typec();
#endif

	netc_init();
	myir_board_init();
	power_on_m7("mx95evkrpmsg");

	return 0;
}

int board_late_init(void)
{
	if (IS_ENABLED(CONFIG_ENV_IS_IN_MMC))
		board_late_mmc_env_init();

	env_set("sec_boot", "no");
#ifdef CONFIG_AHAB_BOOT
	env_set("sec_boot", "yes");
#endif

	myir_set_data_from_eeprom();
	env_set("u-boot_version", PLAIN_VERSION " (" U_BOOT_DATE " - "
		U_BOOT_TIME ")");
	printf("U-Boot build: %s\n", env_get("u-boot_version"));

	return 0;
}

#ifdef CONFIG_OF_BOARD_SETUP
int ft_board_setup(void *blob, struct bd_info *bd)
{
	u64 base[CONFIG_NR_DRAM_BANKS] = { 0 };
	u64 size[CONFIG_NR_DRAM_BANKS] = { 0 };
	char *token, *cursor, *separator;
	int count = 0;

	cursor = env_get("jh_root_mem");
	if (!cursor)
		return 0;

	token = strtok(cursor, ",");
	while (token) {
		if (count >= CONFIG_NR_DRAM_BANKS)
			return -EINVAL;

		separator = token;
		token = strsep(&separator, "@");
		if (!separator)
			return -EINVAL;

		size[count] = simple_strtoull(token, NULL, 16);
		base[count] = simple_strtoull(separator, NULL, 16);
		count++;
		token = strtok(NULL, ",");
	}

	return fdt_fixup_memory_banks(blob, base, size, count);
}
#endif

void board_quiesce_devices(void)
{
	struct uclass *uc_dev;
	int ret;

	ret = uclass_get(UCLASS_PCI, &uc_dev);
	if (uc_dev)
		ret = uclass_destroy(uc_dev);
	if (ret)
		printf("could not remove PCI devices\n");

	ret = imx9_scmi_power_domain_enable(IMX95_PD_HSIO_TOP, false);
	if (ret)
		printf("HSIO power down failed: %d\n", ret);

	ret = imx9_scmi_power_domain_enable(IMX95_PD_NETC, false);
	if (ret)
		printf("NETC power down failed: %d\n", ret);

	ret = uclass_get(UCLASS_SPI_FLASH, &uc_dev);
	if (uc_dev)
		ret = uclass_destroy(uc_dev);
	if (ret)
		printf("could not remove SPI flash devices\n");
}

#if IS_ENABLED(CONFIG_OF_BOARD_FIXUP)
int board_fix_fdt(void *fdt)
{
	board_fix_fdt_fuse(fdt);
	return 0;
}
#endif

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0;
}
#endif
#endif
