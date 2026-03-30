// SPDX-License-Identifier: GPL-2.0
#ifndef QM2MOD_H
#define QM2MOD_H

#undef pr_fmt
#define pr_fmt(fmt) "qm2mod @ %s: " fmt "\n", __func__


#define QM2_PIC_ADDR		0x68	/* I2C addr of the PIC */
#define PIC_REG_PROBE		0x30	/* Probe register - readable if PIC present */
#define PIC_REG_EEPROM_P1A	0x32	/* Model name, first 8 bytes */
#define PIC_REG_EEPROM_P1B	0x33	/* Model name, last 8 bytes */
#define PIC_REG_EEPROM_P2A	0x34	/* Serial number, first 8 bytes */
#define PIC_REG_EEPROM_P2B	0x35	/* Serial number, last 8 bytes */
#define PIC_REG_TEMP1		0x19	/* Temperature sensor 1 */
#define PIC_REG_TEMP2		0x18	/* Temperature sensor 2 */
#define PIC_REG_TEMP3		0x79	/* Temperature sensor 3 */
#define PIC_REG_TEMP4		0x78	/* Temperature sensor 4 */
#define PIC_REG_FAN_SET		0x20	/* Fan PWM write register */
#define PIC_REG_FAN_PWM		0x21	/* Fan PWM read register */
#define PIC_REG_FAN_RPM		0x22	/* Fan speed */
#define PIC_REG_FAN_DEFAULT	0x24	/* Write 0 to restore default fan control */
#define PIC_REG_FW_VER		0xF2	/* PIC firmware version (16-bit) */


#define SUBSYS_REQUIRE		1
#define SUBSYS_EXCLUDE		2

#define QM2_MAX_TEMP		4
#define QM2_MAX_SLOTS		4

struct qm2_card;

struct qm2_gpio_ops {
	int (*get)(struct qm2_card *card, unsigned int offset);
	void (*set)(struct qm2_card *card, unsigned int offset, int value);
	int (*direction_input)(struct qm2_card *card, unsigned int offset);
	int (*direction_output)(struct qm2_card *card, unsigned int offset, int value);
};

struct subsys_entry {
	u16 vendor;
	u16 device;
	int mode;
};

struct qm2_devinfo {
	u16 vendor;
	u16 device;
	int require_bridge;
	int require_devfn_zero;
	const struct subsys_entry  *subsys_list;
	const struct qm2_gpio_ops  *gpio_ops;
	int sda;
	int scl;
	int udelay;
	int needs_mmio;
	int mmio_bar;
	unsigned long mmio_size;
	const char *label_prefix;
};

struct qm2_card {
	struct list_head        list;
	struct pci_dev         *pci_dev;
	const struct qm2_devinfo *info;
	void __iomem           *mmio_base;

	struct i2c_adapter      i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	s32                     i2c_nr;

	struct i2c_client      *pic_client;
	bool                    pic_present;
	char                    model[17];
	char                    serial[17];
	u16                     pic_fw_version;
	int                     num_temp_sensors;
	int                     num_slots;
	bool                    swap_slots;

	struct platform_device *pdev;
};

struct qm2_led {
	struct qm2_card    *card;
	int                 slot;
	u8                  reg;
	char                name[32];
	struct led_classdev cdev;
};

struct qm2_led_type {
	const char *suffix;
	const u8 *regs;
	int max_brightness;
	int (*brightness_set)(struct led_classdev *, enum led_brightness);
};

#endif /* QM2MOD_H */

