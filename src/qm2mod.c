// SPDX-License-Identifier: GPL-2.0
/*
 * qm2mod - Open-source QNAP QM2 expansion card driver
 *
 * QNAP QM2 expansion cards add M.2 SSD slots to QNAP NAS systems.
 * Each card contains a PCIe switch and a PIC microcontroller connected
 * via an I2C bus which is bit-banged over GPIO pins. The I2C bus created
 * by this module is internal to the kernel and is not exposed to userspace.
 *
 * The driver supports the following QM2 card features:
 *  - Exposes sensors via hwmon (temperature, fan RPM, fan PWM)
 *  - Expose per M.2 slot LEDs via the LED subsystem
 *  - Exposes card info via sysfs (model, serial, PIC firmware version)
 *  - Supports reseting fan to default behavior via sysfs
 *
 * Version history:
 *   (pre-git):
 * 		 - Version 0.1 - Initial proof-of-concept on QM2-2P10G1TB
 * 		 - Version 0.2 - Added QM2-4P-384 support, refactored for multiple card models support
 *   1.0 - Initial git release
 * 		   Added support for all known I2C based QM2 models (all except QM2-2S10G1TB02)
 */
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "qm2mod.h"

static bool qm2_preserve_leds = true;
module_param_named(preserve_leds, qm2_preserve_leds, bool, 0);
MODULE_PARM_DESC(preserve_leds, "Preserve LED states on module unload (default on)");


static LIST_HEAD(qm2_card_list);
static int qm2_plat_id;

/* Subsystem filter tables */
static const struct subsys_entry asm2824_subsys[] = {
	{ 0x1BAA, 0xC027, SUBSYS_EXCLUDE },
	{ 0x1BAA, 0xE009, SUBSYS_EXCLUDE },
	{ 0 }
};

static const struct subsys_entry asm2812_subsys[] = {
	{ 0x1BAA, 0xC028, SUBSYS_EXCLUDE },
	{ 0 }
};

static const struct subsys_entry asm0625_subsys[] = {
	{ 0x1BAA, 0x1001, SUBSYS_REQUIRE },
	{ 0x1C04, 0x1001, SUBSYS_REQUIRE },
	{ 0 }
};

static const struct subsys_entry tehuti_subsys[] = {
	{ 0x1FC9, 0x3015, SUBSYS_REQUIRE },
	{ 0 }
};


static inline u32 qm2_gpio_bit(struct qm2_card *card, unsigned int offset)
{
	return (offset == 1) ? card->info->scl : card->info->sda;
}

/* ASMedia ASM2824 PCI switch GPIO backend */

static int qm2_asm2824_get(struct qm2_card *card, unsigned int offset)
{
	u8 tmp = 0;

	pci_read_config_byte(card->pci_dev, 0x0930, &tmp);
	return (tmp >> qm2_gpio_bit(card, offset)) & 1;
}

static void qm2_asm2824_set(struct qm2_card *card, unsigned int offset, int value)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = 0;

	pci_read_config_byte(card->pci_dev, 0x0928, &tmp);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	pci_write_config_byte(card->pci_dev, 0x0928, tmp);
}

static int qm2_asm2824_dir_in(struct qm2_card *card, unsigned int offset)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = 0;

	pci_write_config_byte(card->pci_dev, 0x0FFF, 1);
	pci_read_config_byte(card->pci_dev, 0x0920, &tmp);
	tmp &= ~mask;
	pci_write_config_byte(card->pci_dev, 0x0920, tmp);
	return 0;
}

static int qm2_asm2824_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = 0;

	pci_write_config_byte(card->pci_dev, 0x0FFF, 1);
	pci_read_config_byte(card->pci_dev, 0x0920, &tmp);
	tmp |= mask;
	pci_write_config_byte(card->pci_dev, 0x0920, tmp);
	return 0;
}

static const struct qm2_gpio_ops qm2_asm2824_ops = {
	.get = qm2_asm2824_get,
	.set = qm2_asm2824_set,
	.direction_input = qm2_asm2824_dir_in,
	.direction_output = qm2_asm2824_dir_out,
};

/* IDT/Renesas PCIe Switch GPIO backend */

static int qm2_idt_get(struct qm2_card *card, unsigned int offset)
{
	u32 tmp = 0;

	pci_read_config_dword(card->pci_dev, 0x0420, &tmp);
	return (tmp >> qm2_gpio_bit(card, offset)) & 1;
}

static void qm2_idt_set(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = 1 << qm2_gpio_bit(card, offset);
	u32 tmp = 0;

	pci_read_config_dword(card->pci_dev, 0x0420, &tmp);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	pci_write_config_dword(card->pci_dev, 0x0420, tmp);
}

static int qm2_idt_dir_in(struct qm2_card *card, unsigned int offset)
{
	u32 mask = 1 << qm2_gpio_bit(card, offset);
	u32 tmp = 0;

	pci_read_config_dword(card->pci_dev, 0x0418, &tmp);
	tmp &= ~mask;
	pci_write_config_dword(card->pci_dev, 0x0418, tmp);

	pci_read_config_dword(card->pci_dev, 0x041C, &tmp);
	tmp &= ~mask;
	pci_write_config_dword(card->pci_dev, 0x041C, tmp);
	return 0;
}

static int qm2_idt_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = 1 << qm2_gpio_bit(card, offset);
	u32 tmp = 0;

	pci_read_config_dword(card->pci_dev, 0x0418, &tmp);
	tmp &= ~mask;
	pci_write_config_dword(card->pci_dev, 0x0418, tmp);

	pci_read_config_dword(card->pci_dev, 0x041C, &tmp);
	tmp |= mask;
	pci_write_config_dword(card->pci_dev, 0x041C, tmp);
	return 0;
}

static const struct qm2_gpio_ops qm2_idt_ops = {
	.get = qm2_idt_get,
	.set = qm2_idt_set,
	.direction_input = qm2_idt_dir_in,
	.direction_output = qm2_idt_dir_out
};

/* ASMedia SATA Controller GPIO backend */

static int qm2_asmedia_get(struct qm2_card *card, unsigned int offset)
{
	return (ioread8(card->mmio_base + 0xB43) >> qm2_gpio_bit(card, offset)) & 1;
}

static void qm2_asmedia_set(struct qm2_card *card, unsigned int offset, int value)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = ioread8(card->mmio_base + 0xB42);

	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	iowrite8(tmp, card->mmio_base + 0xB42);
}

static int qm2_asmedia_dir_in(struct qm2_card *card, unsigned int offset)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = ioread8(card->mmio_base + 0xB40);

	tmp &= ~mask;  /* clear bit = input */
	iowrite8(tmp, card->mmio_base + 0xB40);
	return 0;
}

static int qm2_asmedia_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u8 mask = 1 << qm2_gpio_bit(card, offset);
	u8 tmp = ioread8(card->mmio_base + 0xB40);

	tmp |= mask;
	iowrite8(tmp, card->mmio_base + 0xB40);
	return 0;
}

static const struct qm2_gpio_ops qm2_asmedia_ops = {
	.get = qm2_asmedia_get,
	.set = qm2_asmedia_set,
	.direction_input = qm2_asmedia_dir_in,
	.direction_output = qm2_asmedia_dir_out
};

/* PLX PEX 8714/8718 PCIe Switch GPIO backend */

static int qm2_plx_get(struct qm2_card *card, unsigned int offset)
{
	u32 mask = 1 << qm2_gpio_bit(card, offset);

	return (ioread32(card->mmio_base + 0x61C) & mask) ? 1 : 0;
}

static void qm2_plx_set(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = 1 << qm2_gpio_bit(card, offset);
	u32 tmp = ioread32(card->mmio_base + 0x624);

	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	iowrite32(tmp, card->mmio_base + 0x624);
}

static int qm2_plx_dir_in(struct qm2_card *card, unsigned int offset)
{
	u32 tmp = ioread32(card->mmio_base + 0x600);

	if (offset == 0) {
		tmp &= ~0x100;
		tmp &= ~0xC0;
	} else {
		tmp &= ~0x800;
		tmp &= ~0x600;
	}
	iowrite32(tmp, card->mmio_base + 0x600);
	return 0;
}

static int qm2_plx_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u32 tmp = ioread32(card->mmio_base + 0x600);

	if (offset == 0) {
		tmp |= 0x100;
		tmp &= ~0xC0;
	} else {
		tmp |= 0x800;
		tmp &= ~0x600;
	}
	iowrite32(tmp, card->mmio_base + 0x600);
	return 0;
}

static const struct qm2_gpio_ops qm2_plx_ops = {
	.get = qm2_plx_get,
	.set = qm2_plx_set,
	.direction_input = qm2_plx_dir_in,
	.direction_output = qm2_plx_dir_out
};

/* Tehuti GPIO backend */

static inline u32 tehuti_bit(unsigned int offset)
{
	return (offset == 0) ? 0x100 : 0x40;
}

static int qm2_tehuti_get(struct qm2_card *card, unsigned int offset)
{
	iowrite32(0x30000004, card->mmio_base + 0x51E0);
	return (ioread32(card->mmio_base + 0x51F0) & tehuti_bit(offset)) ? 1 : 0;
}

static void qm2_tehuti_set(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = tehuti_bit(offset);
	u32 tmp;

	iowrite32(0x30000004, card->mmio_base + 0x51E0);
	tmp = ioread32(card->mmio_base + 0x51F0);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	iowrite32(0x30010004, card->mmio_base + 0x51E0);
	iowrite32(tmp, card->mmio_base + 0x51F0);
}

static int qm2_tehuti_dir_in(struct qm2_card *card, unsigned int offset)
{
	u32 mask = tehuti_bit(offset);
	u32 tmp;

	iowrite32(0x30000006, card->mmio_base + 0x51E0);
	tmp = ioread32(card->mmio_base + 0x51F0);
	tmp |= mask;
	iowrite32(0x30010006, card->mmio_base + 0x51E0);
	iowrite32(tmp, card->mmio_base + 0x51F0);
	return 0;
}

static int qm2_tehuti_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = tehuti_bit(offset);
	u32 tmp;

	iowrite32(0x30000006, card->mmio_base + 0x51E0);
	tmp = ioread32(card->mmio_base + 0x51F0);
	tmp &= ~mask;
	iowrite32(0x30010006, card->mmio_base + 0x51E0);
	iowrite32(tmp, card->mmio_base + 0x51F0);
	return 0;
}

static const struct qm2_gpio_ops qm2_tehuti_ops = {
	.get = qm2_tehuti_get,
	.set = qm2_tehuti_set,
	.direction_input = qm2_tehuti_dir_in,
	.direction_output = qm2_tehuti_dir_out
};

/* PEX88000 PCIe Switch GPIO backend */

static inline u32 pex88000_bit(unsigned int offset)
{
	return (offset == 0) ? 1 : 2;
}

static int qm2_pex88000_get(struct qm2_card *card, unsigned int offset)
{
	return (ioread32(card->mmio_base + 0x180048) & pex88000_bit(offset)) ? 1 : 0;
}

static void qm2_pex88000_set(struct qm2_card *card, unsigned int offset, int value)
{
	u32 mask = pex88000_bit(offset);
	u32 tmp = ioread32(card->mmio_base + 0x180058);

	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	iowrite32(tmp, card->mmio_base + 0x180058);
}

static int qm2_pex88000_dir_in(struct qm2_card *card, unsigned int offset)
{
	u32 dir_set = pex88000_bit(offset);
	u32 ctrl_clr = (offset == 0) ? ~3U : ~0xCU;

	iowrite32(ioread32(card->mmio_base + 0x180040) | dir_set, card->mmio_base + 0x180040);
	iowrite32(ioread32(card->mmio_base + 0x180000) & ctrl_clr, card->mmio_base + 0x180000);
	return 0;
}

static int qm2_pex88000_dir_out(struct qm2_card *card, unsigned int offset, int value)
{
	u32 dir_clr  = (offset == 0) ? ~1U : ~2U;
	u32 ctrl_set = (offset == 0) ? 3 : 0xC;

	iowrite32(ioread32(card->mmio_base + 0x180040) & dir_clr, card->mmio_base + 0x180040);
	iowrite32(ioread32(card->mmio_base + 0x180000) | ctrl_set, card->mmio_base + 0x180000);
	return 0;
}

static const struct qm2_gpio_ops qm2_pex88000_ops = {
	.get = qm2_pex88000_get,
	.set = qm2_pex88000_set,
	.direction_input = qm2_pex88000_dir_in,
	.direction_output = qm2_pex88000_dir_out
};

/* Device configs */
static const struct qm2_devinfo qm2_devtable[] = {
	/* IDT PCIe Switch (0x806E) - SDA on pin 8 */
	{
		.vendor = 0x111D,
		.device = 0x806E,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_idt_ops,
		.sda = 8,
		.scl = 7,
		.udelay = 5,
		.label_prefix = "idt"
	},
	/* IDT PCIe Switch (all others) - SDA on pin 11 */
	{
		.vendor = 0x111D,
		.device = PCI_ANY_ID,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_idt_ops,
		.sda = 11,
		.scl = 7,
		.udelay = 5,
		.label_prefix = "idt"
	},
	/* ASMedia ASM0625 SATA Controller  */
	{
		.vendor = 0x1B21,
		.device = 0x0625,
		.subsys_list = asm0625_subsys,
		.gpio_ops = &qm2_asmedia_ops,
		.sda = 7,
		.scl = 6,
		.udelay = 50,
		.needs_mmio = 1,
		.mmio_bar = 5,
		.mmio_size = 0x1000,
		.label_prefix = "asmedia"
	},
	/* PLX PEX 8714 PCIe Switch */
	{
		.vendor = 0x10B5,
		.device = 0x8714,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_plx_ops,
		.sda = 2,
		.scl = 3,
		.udelay = 5,
		.needs_mmio = 1,
		.mmio_bar = 0,
		.mmio_size = 0x1000,
		.label_prefix = "plx"
	},
	/* PLX PEX 8718 PCIe Switch */
	{
		.vendor = 0x10B5,
		.device = 0x8718,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_plx_ops,
		.sda = 2,
		.scl = 3,
		.udelay = 5,
		.needs_mmio = 1,
		.mmio_bar = 0,
		.mmio_size = 0x1000,
		.label_prefix = "plx"
	},
	/* ASM2824 PCIe Switch */
	{
		.vendor = 0x1B21,
		.device = 0x2824,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.subsys_list = asm2824_subsys,
		.gpio_ops = &qm2_asm2824_ops,
		.sda = 1,
		.scl = 0,
		.udelay = 5,
		.label_prefix = "asm2824"
	},
	/* ASM1812 PCIe Switch  */
	{
		.vendor = 0x1B21,
		.device = 0x1812,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_asm2824_ops,
		.sda = 1,
		.scl = 0,
		.udelay = 5,
		.label_prefix = "asm1812"
	},
	/* ASM2812 PCIe Switch */
	{
		.vendor = 0x1B21,
		.device = 0x2812,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.subsys_list = asm2812_subsys,
		.gpio_ops = &qm2_asm2824_ops,
		.sda = 1,
		.scl = 0,
		.udelay = 5,
		.label_prefix = "asm2812"
	},
	/* Tehuti w/  10GbE NIC  */
	{
		.vendor = 0x1FC9,
		.device = 0x4027,
		.subsys_list = tehuti_subsys,
		.gpio_ops = &qm2_tehuti_ops,
		.sda = 0,
		.scl = 0,
		.udelay = 50,
		.needs_mmio = 1,
		.mmio_bar = 0,
		.mmio_size = 0x6000,
		.label_prefix = "tehuti"
	},
	/* PEX88000  PCIe Switch  */
	{
		.vendor = 0x1000,
		.device = 0xC010,
		.require_bridge = 1,
		.require_devfn_zero = 1,
		.gpio_ops = &qm2_pex88000_ops,
		.sda = 0,
		.scl = 1,
		.udelay = 50,
		.needs_mmio = 1,
		.mmio_bar = 0,
		.mmio_size = 0x181000,
		.label_prefix = "pex88000"
	}
};

#define QM2_DEVTABLE_SIZE ARRAY_SIZE(qm2_devtable)

/* These are known QM2-2P card models, if a card is not in this list, 
   it's it will have its slots 1 and 2 swapped */
static const char * const qm2_2p_explicit[] = { "QM2-2P10G1T", "QM2-2P10G1TA", "QM2-2P10G1TB", "QM2-2P-344", "QM2-2P-384",
												"QM2-2P-344A", "QM2-2P-384A", "QM2-2P2G2T", "QM2-2P410G1T", "QM2-2P410G2T", 
												"QM2-2P-244A-A1", NULL};

static void qm2_i2c_setsda(void *data, int state)
{
	struct qm2_card *card = data;

	if (state)
		card->info->gpio_ops->direction_input(card, 0);
	else {
		card->info->gpio_ops->set(card, 0, 0);
		card->info->gpio_ops->direction_output(card, 0, 0);
	}
}

static void qm2_i2c_setscl(void *data, int state)
{
	struct qm2_card *card = data;

	if (state)
		card->info->gpio_ops->direction_input(card, 1);
	else {
		card->info->gpio_ops->set(card, 1, 0);
		card->info->gpio_ops->direction_output(card, 1, 0);
	}
}

static int qm2_i2c_getsda(void *data)
{
	return ((struct qm2_card *)data)->info->gpio_ops->get(data, 0);
}

static int qm2_i2c_getscl(void *data)
{
	return ((struct qm2_card *)data)->info->gpio_ops->get(data, 1);
}


static void qm2_pci_dev_put(void *data)
{
	pci_dev_put(data);
}

static void qm2_i2c_del_adapter(void *data)
{
	i2c_del_adapter(data);
}

static int qm2_check_subsystem(const struct qm2_devinfo *info, struct pci_dev *dev)
{
	const struct subsys_entry *e;
	bool has_require = false;
	bool match;

	if (!info->subsys_list)
		return 1;

	for (e = info->subsys_list; e->vendor; e++) {
		match = (dev->subsystem_vendor == e->vendor &&
			 dev->subsystem_device == e->device);

		if (e->mode == SUBSYS_EXCLUDE && match) {
			pr_debug("subsys %04x:%04x excluded by %04x:%04x", dev->subsystem_vendor, dev->subsystem_device, e->vendor, e->device);
			return 0;
		}

		if (e->mode == SUBSYS_REQUIRE) {
			has_require = true;
			if (match) {
				pr_debug("subsys %04x:%04x matched required %04x:%04x", dev->subsystem_vendor, dev->subsystem_device, e->vendor, e->device);
				return 1;
			}
		}
	}

	if (has_require)
		pr_debug("subsys %04x:%04x did not match any required entry", dev->subsystem_vendor, dev->subsystem_device);

	return has_require ? 0 : 1;
}


static int qm2_match_pci_device(const struct qm2_devinfo *info, struct pci_dev *dev)
{
	resource_size_t len;

	if (dev->vendor != info->vendor)
		return 0;

	pr_debug("Checking %04x:%04x against table %04x:%04x", dev->vendor, dev->device, info->vendor, info->device);
	if (info->device != PCI_ANY_ID && dev->device != info->device) {
		pr_debug("rejected: device ID mismatch");
		return 0;
	}

	if (info->require_bridge && ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)) {
		pr_debug("rejected: not a PCI bridge (class 0x%04x)", dev->class >> 8);
		return 0;
	}

	if (info->require_devfn_zero && dev->devfn != 0) {
		pr_debug("rejected: devfn %d (need 0)", dev->devfn);
		return 0;
	}

	if (info->needs_mmio) {
		len = pci_resource_len(dev, info->mmio_bar);

		if (!pci_resource_start(dev, info->mmio_bar) || len < info->mmio_size) {
			pr_debug("rejected: BAR%d too small (%llu < %lu)", info->mmio_bar, (unsigned long long)len, info->mmio_size);
			return 0;
		}
	}

	if (!qm2_check_subsystem(info, dev)) {
		pr_debug("rejected: subsystem filter");
		return 0;
	}

	pr_info("matched (%s)", info->device == PCI_ANY_ID ? "wildcard" : "exact");
	return 1;
}

static void qm2_identify_card(struct qm2_card *card)
{
	int i;

	card->num_temp_sensors = 2;
	card->num_slots = 2;
	card->swap_slots = false;

	if (strncmp(card->model, "QM2-4", 5) == 0) {
		card->num_temp_sensors = 4;
		card->num_slots = 4;
	} else if (strncmp(card->model, "QM2-2P", 6) == 0) {
		/* Assume generic (swapped) until proven otherwise */
		card->swap_slots = true;
		for (i = 0; qm2_2p_explicit[i]; i++) {
			if (strcmp(card->model, qm2_2p_explicit[i]) == 0) {
				card->swap_slots = false;
				break;
			}
		}
	}

	pr_info("Card: %s serial=%s temp=%d slots=%d swap=%d", card->model, card->serial, card->num_temp_sensors, card->num_slots, card->swap_slots);
}


static int qm2_read_eeprom_page(struct i2c_client *client, u8 reg_a, u8 reg_b, char *buf, int maxlen)
{
	u8 tmp[I2C_SMBUS_BLOCK_MAX];
	int ret, len = 0, n;

	ret = i2c_smbus_read_block_data(client, reg_a, tmp);
	if (ret < 0)
		return ret;
	n = min(ret, maxlen);
	memcpy(buf, tmp, n);
	len += n;

	if (len < maxlen) {
		ret = i2c_smbus_read_block_data(client, reg_b, tmp);
		if (ret < 0)
			return len;
		n = min(ret, maxlen - len);
		memcpy(buf + len, tmp, n);
		len += n;
	}

	return len;
}

static int qm2_probe_pic(struct qm2_card *card)
{
	u8 probe_buf[8];
	int ret;

	card->pic_client = i2c_new_dummy_device(&card->i2c_adap, QM2_PIC_ADDR);
	if (IS_ERR(card->pic_client)) {
		ret = PTR_ERR(card->pic_client);
		card->pic_client = NULL;
		return ret;
	}

	/* Attempt to read the probe register, fails if no PIC present (indicate phantom)*/
	ret = i2c_smbus_read_i2c_block_data(card->pic_client, PIC_REG_PROBE, sizeof(probe_buf), probe_buf);
	if (ret < 0) {
		pr_debug("No PIC at 0x%02x on adapter %d", QM2_PIC_ADDR, card->i2c_nr);
		i2c_unregister_device(card->pic_client);
		card->pic_client = NULL;
		return -ENODEV;
	}

	card->pic_present = true;

	/* Read card identity from EEPROM */
	memset(card->model, 0, sizeof(card->model));
	qm2_read_eeprom_page(card->pic_client, PIC_REG_EEPROM_P1A, PIC_REG_EEPROM_P1B, card->model, 16);

	memset(card->serial, 0, sizeof(card->serial));
	qm2_read_eeprom_page(card->pic_client, PIC_REG_EEPROM_P2A, PIC_REG_EEPROM_P2B, card->serial, 16);

	ret = i2c_smbus_read_word_data(card->pic_client, PIC_REG_FW_VER);
	if (ret >= 0)
		card->pic_fw_version = (u16)ret;

	qm2_identify_card(card);

	pr_info("PIC on adapter %d: model=%s serial=%s fw=%d.%d", card->i2c_nr, card->model, card->serial, card->pic_fw_version >> 8, card->pic_fw_version & 0xFF);
	return 0;
}

static const u8 qm2_temp_regs[QM2_MAX_TEMP] = {
	PIC_REG_TEMP1, PIC_REG_TEMP2, PIC_REG_TEMP3, PIC_REG_TEMP4
};

static umode_t qm2_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
	const struct qm2_card *card = data;

	switch (type) {
	case hwmon_temp:
		return (channel < card->num_temp_sensors) ? 0444 : 0;
	case hwmon_fan:
		return 0444;
	case hwmon_pwm:
		return 0644;
	default:
		return 0;
	}
}

static int qm2_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val)
{
	struct qm2_card *card = dev_get_drvdata(dev);
	int ret, ch;

	if (!card->pic_client)
		return -ENODEV;

	switch (type) {
	case hwmon_temp:
		ch = channel;
		if (ch >= card->num_temp_sensors)
			return -EINVAL;
		if (card->swap_slots && ch < 2)
			ch ^= 1;
		ret = i2c_smbus_read_byte_data(card->pic_client, qm2_temp_regs[ch]);
		if (ret < 0)
			return ret;
		*val = ret * 1000;
		return 0;

	case hwmon_fan:
		ret = i2c_smbus_read_byte_data(card->pic_client, PIC_REG_FAN_RPM);
		if (ret < 0)
			return ret;
		*val = ret * 60; 
		return 0;

	case hwmon_pwm:
		ret = i2c_smbus_read_byte_data(card->pic_client, PIC_REG_FAN_PWM);
		if (ret < 0)
			return ret;
		*val = ret;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int qm2_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long val)
{
	struct qm2_card *card = dev_get_drvdata(dev);

	if (!card->pic_client)
		return -ENODEV;

	if (type == hwmon_pwm) {
		val = clamp_val(val, 0, 255);
		return i2c_smbus_write_byte_data(card->pic_client, PIC_REG_FAN_SET, val);
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_channel_info * qm2_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_ops qm2_hwmon_ops = {
	.is_visible = qm2_hwmon_is_visible,
	.read = qm2_hwmon_read,
	.write = qm2_hwmon_write
};

static const struct hwmon_chip_info qm2_hwmon_chip_info = {
	.ops = &qm2_hwmon_ops,
	.info = qm2_hwmon_info
};

static int qm2_register_hwmon(struct qm2_card *card)
{
	struct device *hwmon;

	hwmon = devm_hwmon_device_register_with_info(
		&card->pdev->dev, "qm2", card, &qm2_hwmon_chip_info, NULL);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	pr_info("Registered hwmon for adapter %d", card->i2c_nr);
	return 0;
}


static int qm2_led_error_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	u8 val = 1;
	struct qm2_led *led = container_of(cdev, struct qm2_led, cdev);

	if (cdev->flags & LED_UNREGISTERING) {
		if (qm2_preserve_leds)
			return 0;
	}

	if (brightness == 0) {
		val = 1;
	} else if (brightness == 1) {
		val = 0;
	} else if (brightness == 2) {
		val = 3;
	} 

	if (!led->card->pic_client)
		return -ENODEV;
	return i2c_smbus_write_byte_data(led->card->pic_client, led->reg, val);
}

static int qm2_led_present_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	u8 val = 1;
	struct qm2_led *led = container_of(cdev, struct qm2_led, cdev);

	if (cdev->flags & LED_UNREGISTERING) {
		if (qm2_preserve_leds)
			return 0;
	}

	if (brightness == 0) {
		val = 1;
	} else if (brightness == 1) {
		val = 0;
	} else if (brightness == 2) {
		val = 3;
	} 

	if (!led->card->pic_client)
		return -ENODEV;

	return i2c_smbus_write_byte_data(led->card->pic_client, led->reg, val);
}

static const u8 led_present_regs[] = { 0x15, 0x14, 0x75, 0x74 };
static const u8 led_error_regs[] = { 0x11, 0x10, 0x71, 0x70 };
static const struct qm2_led_type qm2_led_types[] = {
	{ "red", led_error_regs, 2, qm2_led_error_set},
	{ "green", led_present_regs, 2, qm2_led_present_set }
};
#define QM2_LEDS_PER_SLOT ARRAY_SIZE(qm2_led_types)

static int qm2_register_leds(struct qm2_card *card)
{
	struct device *dev = &card->pdev->dev;
	struct qm2_led *leds;
	struct qm2_led *led;
	int slot, t, idx = 0, ret;
	int num_leds = card->num_slots * QM2_LEDS_PER_SLOT;
	int reg_idx;

	leds = devm_kcalloc(dev, num_leds, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (slot = 0; slot < card->num_slots; slot++) {
		reg_idx = slot;

		if (card->swap_slots && slot < 2)
			reg_idx = slot ^ 1;

		for (t = 0; t < QM2_LEDS_PER_SLOT; t++) {
			led = &leds[idx];

			led->card = card;
			led->slot = slot;
			led->reg = qm2_led_types[t].regs[reg_idx];

			snprintf(led->name, sizeof(led->name), "qm2-%d:%s:slot%d", card->i2c_nr, qm2_led_types[t].suffix, slot + 1);

			led->cdev.name = led->name;
			led->cdev.max_brightness = qm2_led_types[t].max_brightness;
			led->cdev.brightness_set_blocking = qm2_led_types[t].brightness_set;

			ret = devm_led_classdev_register(dev, &led->cdev);
			if (ret) {
				pr_err("Failed to register LED %s: %d", led->name, ret);
				return ret;
			}
			idx++;
		}
	}

	pr_info("Registered %d LEDs for adapter %d", idx, card->i2c_nr);
	return 0;
}

static ssize_t model_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", ((struct qm2_card *)dev_get_drvdata(dev))->model);
}
static DEVICE_ATTR_RO(model);

static ssize_t serial_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", ((struct qm2_card *)dev_get_drvdata(dev))->serial);
}
static DEVICE_ATTR_RO(serial);

static ssize_t pic_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qm2_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d.%d\n", card->pic_fw_version >> 8, card->pic_fw_version & 0xFF);
}
static DEVICE_ATTR_RO(pic_version);

static ssize_t fan_default_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qm2_card *card = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	if (!card->pic_client)
		return -ENODEV;

	ret = kstrtoul(buf, 10, &val);
	if (ret || val != 1)
		return -EINVAL;

	/* Writing 0 to register 0x24 restores default fan control */
	ret = i2c_smbus_write_byte_data(card->pic_client, PIC_REG_FAN_DEFAULT, 0);
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_WO(fan_default);

static struct attribute *qm2_card_attrs[] = {
	&dev_attr_model.attr,
	&dev_attr_serial.attr,
	&dev_attr_pic_version.attr,
	&dev_attr_fan_default.attr,
	NULL
};

static const struct attribute_group qm2_card_attr_group = {
	.attrs = qm2_card_attrs
};

static const struct attribute_group *qm2_plat_attr_groups[] = {
	&qm2_card_attr_group,
	NULL
};

static struct qm2_card *qm2_setup_card(const struct qm2_devinfo *info, struct pci_dev *pci_dev)
{
	struct platform_device *pdev;
	struct qm2_card *card;
	struct pci_bus *bus;
	resource_size_t start, len;
	int ret;

	pdev = platform_device_alloc("qm2", qm2_plat_id++);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	pdev->dev.groups = qm2_plat_attr_groups;

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		ret = -ENOMEM;
		goto err_pdev;
	}

	card->pci_dev = pci_dev_get(pci_dev);
	card->info = info;
	card->pdev = pdev;
	bus = pci_dev->bus;

	ret = devm_add_action_or_reset(&pdev->dev, qm2_pci_dev_put, card->pci_dev);
	if (ret)
		goto err_pdev;

	if (info->needs_mmio) {
		start = pci_resource_start(pci_dev, info->mmio_bar);
		len = pci_resource_len(pci_dev, info->mmio_bar);

		if (!start || len < info->mmio_size) {
			pr_err("BAR%d unavailable or too small for %04x:%04x", info->mmio_bar, pci_dev->vendor, pci_dev->device);
			ret = -ENODEV;
			goto err_pdev;
		}
		card->mmio_base = devm_ioremap(&pdev->dev, start, info->mmio_size);
		if (!card->mmio_base) {
			pr_err("ioremap failed for BAR%d of %04x:%04x", info->mmio_bar, pci_dev->vendor, pci_dev->device);
			ret = -ENOMEM;
			goto err_pdev;
		}
	}

	card->i2c_algo.setsda = qm2_i2c_setsda;
	card->i2c_algo.setscl = qm2_i2c_setscl;
	card->i2c_algo.getsda = qm2_i2c_getsda;
	card->i2c_algo.getscl = qm2_i2c_getscl;
	card->i2c_algo.udelay = info->udelay;
	card->i2c_algo.timeout = msecs_to_jiffies(100);
	card->i2c_algo.data = card;
	card->i2c_adap.owner = THIS_MODULE;
	card->i2c_adap.algo_data = &card->i2c_algo;
	card->i2c_adap.dev.parent = &pci_dev->dev;

	snprintf(card->i2c_adap.name, sizeof(card->i2c_adap.name), "qm2-%s-%04x:%02x", info->label_prefix, pci_domain_nr(bus), bus->number);

	ret = i2c_bit_add_bus(&card->i2c_adap);
	if (ret) {
		pr_err("i2c_bit_add_bus failed: %d", ret);
		goto err_pdev;
	}

	ret = devm_add_action_or_reset(&pdev->dev, qm2_i2c_del_adapter, &card->i2c_adap);
	if (ret)
		goto err_pdev;

	card->i2c_nr = card->i2c_adap.nr;
	dev_set_drvdata(&pdev->dev, card);

	/* Probe the PIC microcontroller over I2C */
	ret = qm2_probe_pic(card);
	if (ret < 0) {
		/* No PIC found, avoid creating phantom device */
		goto err_pdev;
	}

	/* PIC found, register the platform device and components */
	ret = platform_device_add(pdev);
	if (ret)
		goto err_pdev;

	ret = qm2_register_hwmon(card);
	if (ret)
		goto err_pdev;

	ret = qm2_register_leds(card);
	if (ret)
		goto err_pdev;

	pr_info("Attached %04x:%04x [%04x:%04x] SDA=%d SCL=%d i2c=%d", pci_dev->vendor, pci_dev->device,
				pci_dev->subsystem_vendor, pci_dev->subsystem_device, info->sda, info->scl, card->i2c_nr);

	return card;

err_pdev:
	platform_device_put(pdev);
	return ERR_PTR(ret);
}

static int __init qm2mod_init(void)
{
	struct pci_dev *pci_dev;
	struct qm2_card *card;
	const struct qm2_devinfo *match;
	size_t i;
	int card_count = 0;

	pr_info("Loading module");

	/* Scan all PCI devices, match against device table. */
	pci_dev = NULL;
	while ((pci_dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pci_dev)) != NULL) {
		match = NULL;

		for (i = 0; i < QM2_DEVTABLE_SIZE; i++) {
			if (!qm2_match_pci_device(&qm2_devtable[i], pci_dev))
				/* no match, skip */
				continue;
			if (qm2_devtable[i].device != PCI_ANY_ID) {
				/* exact match, can't do better */
				match = &qm2_devtable[i];
				break;
			}
			if (!match)
				/* matched a wildcard */
				match = &qm2_devtable[i];
		}

		if (match) {
			card = qm2_setup_card(match, pci_dev);
			/* Only keep the device if the PIC was found and setup succeeded,
			   this prevents a phantom device from being added due to
			   upstream/downstream ports sharing the same PCI ID.
			   From what I see in the QNAPs original code, even they dont have this 
			   filter feature! */
			if (!IS_ERR(card)) {
				list_add_tail(&card->list, &qm2_card_list);
				card_count++;
			}
		}
	}

	if (card_count == 0) {
		pr_info("No compatible QM2 devices found");
		return 0;
	}

	pr_info("Module loaded with %d QM2 device(s)", card_count);
	return 0;
}

static void __exit qm2mod_exit(void)
{
	struct qm2_card *card, *tmp;
	struct platform_device *pdev;

	pr_info("Unloading module");

	list_for_each_entry_safe(card, tmp, &qm2_card_list, list) {
		pdev = card->pdev;

		list_del(&card->list);
		platform_device_unregister(pdev);
	}

	pr_info("Module unloaded");
}

module_init(qm2mod_init);
module_exit(qm2mod_exit);

MODULE_DESCRIPTION("QNAP QM2 expansion card driver");
MODULE_VERSION("1.0");
MODULE_AUTHOR("0xGiddi");
MODULE_LICENSE("GPL");

