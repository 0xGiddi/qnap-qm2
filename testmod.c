// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux kernel module for controlling QNAP QM2 cards.
 *
 * Version history:
 *      0.0: WIP
 */

#include <linux/module.h>



static struct qm2_data {
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
}

int direction_input(struct gpio_chip *gc, unsigned int offset) {
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);
    u8 val = 0x7f;

    if (offset) {
        val = 0xfe;
        if (offset == 1)
            val = 0xbf;
    }
    iowrite8(ioread8(data->remap_addr + 0xb40) & val, data->remap_addr);
    return 0;
}

int direction_output(struct gpio_chip *gc, unsigned int offset, int value) {
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);
    u8 val = 0x80;
    u8 tmp = 0x80;

    if (offset) {
        val = 1;
        if (offset == 1)
            val = 0x40;
    }
    tmp = val | ioread8(data->remap_addr + 0xb40);
    if (value)
        tmp = ioread8(data->remap_addr + 0xb40) & ~val;
    iowrite8(tmp, data->remap_addr);
    return 0;
}

.. aybe set multiple
int get_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits) {
    struct qm2_data *data = container_of(gc, struct qm2_data, chip);
    u8 val;

    val = 7;
    if (mask) {
        val = 6;
        if (mask != 2)
            val = 0;
    }
    return (ioread8(data->remap_addr + 0xb40) >> val) & 1;
}



static int __init init(void) {
    int ret;
    struct pci_dev *dev;
    struct qm2_data data;
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
        data->label = kasprintf("gpio-asm2824_%04x:%02x_%d", ??, ??, data->i2c_adapter_nr)
        if (!data->label) {
            kfree(data);
            return -ENOMEM;
        }
        data->chip.label = data->label;
        data->chip.owner = THIS_MODULE;
        data->chip.direction_input =    // asm2824_gpio_direction_input
        data->chip.get_multiple =       // asm2824_gpio_get_value
        data->chip.direction_output =   // asm2824_gpio_config
        data->chip.set_multiple =       // asm2824_gpio_set_value
        data->chip.base = -1;
        data->chip.ngpio = 2;
        
        ret = gpiochip_add_data(data->chip, NULL);
        if (ret) {
            kfree(data->label);
            kfree(data);
            return ret;
        }

        data->plat_dev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
        if (!data->plat_dev) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(data->chip);
            return ret;
        }

        data->plat_data = kzalloc(sizeof(struct i2c_gpio_platform_data), GFP_KERNEL);
        if (!data->plat_dev) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(data->chip);
            kfree(data->plat_dev);
            return ret;
        }
        data->plat_data->udelay = 5; // Also all the bitfields are set to 0 by kzalloc

        ret = platform_device_add_data(data->plat_dev, &data->plat_data, sizeof(i2c_gpio_platform_data))
        if (ret) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(data->chip);
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
            gpiochip_remove(data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            return -ENOMEM;
        }
        
        lookup->dev_id = kasprintf("i2c-gpio.%d"m data->i2c_adapter_nr);
        if (!lookup->dev_id) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(data->chip);
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

        ret = platform_device_register(data->plat_dev)
        if (ret) {
            kfree(data->label);
            kfree(data);
            gpiochip_remove(data->chip);
            kfree(data->plat_dev);
            kfree(data->plat_data);
            kfree(lookup->dev_id);
            kfree(lookup);
            return -ENOMEM;
        }
}

static void __exit exit(void) {

}


module_init(init);
module_exit(exit);

MODULE_DESCRIPTION("QNAP QM2 I2C Driver");
MODULE_VERSION("0.0");
MODULE_AUTHOR("0xGiddi - <qnap8528 AT giddi.net>");
MODULE_LICENSE("GPL");