// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux kernel module for controlling QNAP QM2 cards.
 *
 * Version history:
 *      0.0: WIP
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-gpio.h>
#include <linux/gpio/machine.h>


struct qm2_data {
    struct list_head list;      // 0 8
    struct pci_dev *pci_dev;    // 16
    char *label;                // 24
    void __iomem *remap_addr;   // 32
    struct gpio_chip chip;      // 40
    
    struct platform_device *plat_dev;
    struct i2c_gpio_platform_data *plat_data;

    u32 sda;                    // 29
    u32 scl;                    // 29.5
    u32 i2c_adapter_nr;         // 30
};

static int get_next_i2c_adapter(void) {
    u8 i;
    struct i2c_adapter *adapter;

    for (i=32; i<=64; i++) {
        adapter = i2c_get_adapter(i);
        if (!adapter)
            return i;
        i2c_put_adapter(adapter);
    }
    return -ENODEV;
}


int set_direction(struct pci_dev *gc, unsigned int offset, int value)
{
    u8 tmp;
    u8 mask = 1;
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);
    

    pci_write_config_byte(data->pci_dev, 0x0fff, 1);
    if (offset == 1) {
            mask = 1 << data->sda;
    } else {
        mask = 1 << data->scl;
    }
    
    pci_read_config_byte(data->pci_dev, 0x0920, &tmp);
    
    if (value)
        tmp = ~mask & tmp;
    else
        tmp = mask | tmp;
    
    pci_write_config_byte(daa->pci_dev, 0x0920, tmp);
}

int get(struct gpio_chip *gc, unsigned int offset) {
    u8 tmp;
    u8 shift = 0;
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);
    
    if (offset == 1)
        shift = data->sda_pin;
    else
        shift = data->scl_pin;

    pci_read_config_byte(data->pci_dev, 0x0930, &tmp);
    return (tmp >> shift) & 1;
}

void set(struct gpio_chip *gc, unsigned int offset, int value) {
    u8 tmp;
    u8 mask = 1;
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);

    if (offset == 1)
        mask = 1 << data->sda_pin;
    else
        mask = 1 << data->scl_pin;
    
    pci_read_config_byte(data->pci_dev, 0x0928, &tmp);
    if (value)
        tmp = tmp | mask;
    else
        tmp = tmp & ~mask;

    pci_write_config_byte(data->pci_dev, 0x0928, tmp);
}

int direction_input(struct gpio_chip *gc, unsigned int offset) {
    return set_direction(gc, offset, 1);
}

int direction_output(struct pci_dev *gc, unsigned int offset, int value) {
    return set_direction(gc, offset, value);
}



static int __init test_init(void) {
    int ret;
    struct pci_dev *dev;
    struct qm2_data *data;
    struct gpiod_lookup_table *lookup;

    while ((dev = pci_get_device(0x1b21, 0x2824, dev)) != NULL) {
        if ((dev->devfn) ||
            ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI) || 
            (dev->subsystem_vendor == 0x1baa && 
            (dev->subsystem_device == 0xc027 || dev->subsystem_device == 0xe009)))
        continue;

        data = kzalloc(sizeof(struct qm2_data), GFP_KERNEL);
        if (!data)
            return -ENOMEM;

        data->pci_dev = dev;
        data->sda = 1;
        data->scl = 0;
        data->i2c_adapter_nr = get_next_i2c_adapter();
	// TODO: Fix 0 0 
        data->label = kasprintf(GFP_KERNEL, "gpio-asm2824_%04x:%02x_%d", 0, 0, data->i2c_adapter_nr);
        if (!data->label) {
            kfree(data);
            return -ENOMEM;
        }
        data->chip.label = data->label;
        data->chip.owner = THIS_MODULE;
        data->chip.direction_input = direction_input;  // asm2824_gpio_direction_input
        data->chip.get= get;  // asm2824_gpio_get_value
        data->chip.direction_output = direction_output; // asm2824_gpio_config
        data->chip.set = set;// asm2824_gpio_set_value
        data->chip.base = -1;
        data->chip.ngpio = 2;
        
        ret = gpiochip_add_data(&data->chip, NULL);
        if (ret) {
            kfree(data->label);
            kfree(data);
            return ret;
        }

        data->plat_dev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
        if (!data->plat_dev) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            return ret;
        }

        data->plat_data = kzalloc(sizeof(struct i2c_gpio_platform_data), GFP_KERNEL);
        if (!data->plat_dev) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            kfree(data->plat_dev);
            return ret;
        }
        data->plat_data->udelay = 5; // Also all the bitfields are set to 0 by kzalloc

        ret = platform_device_add_data(data->plat_dev, &data->plat_data, sizeof(struct i2c_gpio_platform_data));
        if (ret) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            return ret;
        }

        data->plat_dev->id = data->i2c_adapter_nr;
        // TODO: devm or free pointer
        lookup = kzalloc(struct_size(lookup, table, 3), GFP_KERNEL);
        if (!lookup) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            return -ENOMEM;
        }
        
        lookup->dev_id = kasprintf(GFP_KERNEL, "i2c-gpio.%d", data->i2c_adapter_nr);
        if (!lookup->dev_id) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            kfree(lookup);
            return -ENOMEM;
        }

        lookup->table[0].key = "";
        lookup->table[0].chip_hwnum = 0;
        lookup->table[0].con_id = 0;
        lookup->table[0].idx = 0;
        lookup->table[0].flags = 0;
        lookup->table[1].key = "";
        lookup->table[1].chip_hwnum = 1;
        lookup->table[1].con_id = 0;
        lookup->table[1].idx = 1;
        lookup->table[1].flags = 0;
        gpiod_add_lookup_table(lookup);

        ret = platform_device_register(data->plat_dev);
        if (ret) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(&data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            kfree(lookup->dev_id);
            kfree(lookup);
            return -ENOMEM;
        }
    }
    return 0;
}

static void __exit test_exit(void) {
    return;
}


module_init(test_init);
module_exit(test_exit);

MODULE_DESCRIPTION("QNAP QM2 I2C Driver");
MODULE_VERSION("0.0");
MODULE_AUTHOR("0xGiddi - <qnap8528 AT giddi.net>");
MODULE_LICENSE("GPL");
