#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-gpio.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/driver.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s @ %s: " fmt "\n", DRVNAME, __func__
#define DRVNAME "QM2mod"

struct qm2_data {
    struct list_head list;
    struct pci_dev *pci_dev;
    char *label;
    struct gpio_chip gchip;
    struct platform_device *plat_dev;
    struct i2c_gpio_platform_data *gpio_plat_data;
    struct gpiod_lookup_table *lookup_table;
    u32 sda;
    u32 scl;
    s32 i2c_nr;
};

static LIST_HEAD(qm2mod_data_list);

static int qm2mod_asm2824_set_dir(struct gpio_chip *gc, unsigned int offset, int value) {
    u8 tmp = 0;
    u8 mask = 1;
    struct qm2_data *data = gpiochip_get_data(gc);

    //pr_debug("Setting direction of offset %d to %d (data is at %p)", offset, value, (void *)&data);
    
    pci_write_config_byte(data->pci_dev, 0x0fff, 1);

    if (offset == 1)
        mask = 1 << data->scl;
    else
        mask = 1 << data->sda;
    
    pci_read_config_byte(data->pci_dev, 0x0920, &tmp);
    if (value)
        tmp &= ~mask;
    else
        tmp |= mask;
    pci_write_config_byte(data->pci_dev, 0x0920, tmp);

    return 0;
}

static int qm2mod_asm2824_set_dir_in(struct gpio_chip *gc, unsigned int offset) {
    return qm2mod_asm2824_set_dir(gc, offset, 1);
}

static int qm2mod_asm2824_set_dir_out(struct gpio_chip *gc, unsigned int offset, int value) {
    return qm2mod_asm2824_set_dir(gc, offset, value);
}

static int qm2mod_asm2824_get(struct gpio_chip *gc, unsigned int offset) {
    u8 tmp = 0;
    u8 shift = 0;
    struct qm2_data *data = gpiochip_get_data(gc);

    //pr_debug("Getting value at offset %d (data is at %p)", offset, (void *)&data);
    if (offset == 1)
        shift = data->scl;
    else
        shift = data->sda;

    pci_read_config_byte(data->pci_dev, 0x0930, &tmp);
    return (tmp >> shift) & 1;
}

static void qm2mod_asm2824_set(struct gpio_chip *gc, unsigned int offset, int value) {
    u8 tmp = 0;
    u8 mask = 1;
    struct qm2_data *data = gpiochip_get_data(gc);

    //pr_debug("Setting value at offset %d to %d (data is at %p)", offset, value, (void *)&data);
    if (offset == 1)
        mask = 1 << data->scl;
    else
        mask = 1 << data->sda;
    pci_read_config_byte(data->pci_dev, 0x0928, &tmp);
    if (value)
        tmp |= mask;
    else
        tmp &= ~mask;
    pci_write_config_byte(data->pci_dev, 0x0928, tmp);
}

static int qm2mod_get_i2c_adapter(void) {
    u8 i;
    struct i2c_adapter *adapt;

    for (i=32; i<=64; i++) {
        adapt = i2c_get_adapter(i);
        if (!adapt) {
            pr_debug("Found next available I2C adapter: %d", i);
            return i;
        }
        i2c_put_adapter(adapt);
    }
    pr_debug("Could not find free I2C adapter number");
    return -ENODEV;
}

static int __init qm2mod_init(void) {
    int ret;
    struct pci_dev *dev = NULL;
    struct qm2_data *data = NULL;

    
    while ((dev = pci_get_device(0x1b21, 0x2824, dev)) != NULL) {
        /* Check if we actually need to create an I2C adapter for this device */
        if ((dev->devfn) ||
            ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI) || 
            (dev->subsystem_vendor == 0x1baa && 
            (dev->subsystem_device == 0xc027 || dev->subsystem_device == 0xe009))) {
            pr_debug("Skippig PCI device %04x:%04x [Subsystem %04x:%04x, DevFn: %08x, Class: %04x]", dev->vendor, dev->device, dev->subsystem_vendor, dev->subsystem_device, dev->devfn, dev->class);
            continue;
        }
        pr_debug("Found PCI device %04x:%04x [Subsystem %04x:%04x, DevFn: %08x, Class: %04x]", dev->vendor, dev->device, dev->subsystem_vendor, dev->subsystem_device, dev->devfn, dev->class);
        
        /* Allocate QM2 data */
        data = kzalloc(sizeof(struct qm2_data), GFP_KERNEL);
        if (!data) {
            pr_debug("Failed to allocate memory for QM2 data");
            continue;
        }
        pr_debug("Data is at: %p", (void *)&data);
        /* Save the PCI device */
        data->pci_dev = dev;
        /* Initialize information out I2C on this device */
        data->sda = 1;
        data->scl = 0;
        data->i2c_nr = qm2mod_get_i2c_adapter();
        data->label = kasprintf(GFP_KERNEL, "gpio-asm2824_%04x:%02x_%d", dev->bus->number, PCI_SLOT(dev->devfn), data->i2c_nr);
        if (!data->label) {
            pr_debug("Failed to alocate label");
            kfree(data);
            continue;
        }
        pr_debug("Allocated label '%s'", data->label);
        /* Initialize and register the gpio chip */
        data->gchip.label = data->label;
        data->gchip.owner = THIS_MODULE;
        data->gchip.direction_input = qm2mod_asm2824_set_dir_in;
        data->gchip.direction_output = qm2mod_asm2824_set_dir_out;
        data->gchip.get = qm2mod_asm2824_get;
        data->gchip.set = qm2mod_asm2824_set;
        data->gchip.base = -1;
        data->gchip.ngpio = 2;

        ret = gpiochip_add_data(&data->gchip, data);
        if (ret) {
            pr_debug("Add gpio chip failed");
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Added GPIO chip");

        /* Initialize the platform device and data*/
        data->plat_dev = platform_device_alloc("i2c-gpio", data->i2c_nr); //kzalloc(sizeof(struct platform_device), GFP_KERNEL);
        if (!data->plat_dev) {
            pr_debug("Platform device memory allocation failed");
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        //data->plat_dev->name = "i2c-gpio";
        //data->plat_dev->id = data->i2c_nr;
        pr_debug("Allocated platform device");

        /* kzalloc make sure that unset fields are 0, important! */
        data->gpio_plat_data = kzalloc(sizeof(struct i2c_gpio_platform_data), GFP_KERNEL);
        if (!data->gpio_plat_data) {
            pr_debug("Failed to allocate gpio platform data");
            platform_device_put(data->plat_dev);
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Allocated gpio platform device data");
        data->gpio_plat_data->udelay = 5;

        ret = platform_device_add_data(data->plat_dev, data->gpio_plat_data, sizeof(struct i2c_gpio_platform_data));
        if (ret) {
            pr_debug("Failed to add platform device data");
            kfree(data->gpio_plat_data);
            platform_device_put(data->plat_dev);
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Added platform device gpio data");
        
        /* Initialize and register gpiod lookup table */
        data->lookup_table = kzalloc(struct_size(data->lookup_table, table, 3), GFP_KERNEL);
        if (!data->lookup_table) {
            pr_debug("Failed to allocate lookup table");
            platform_device_put(data->plat_dev);
            kfree(data->gpio_plat_data);
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Allocated lookup table");

        data->lookup_table->dev_id = kasprintf(GFP_KERNEL, "i2c-gpio.%d", data->i2c_nr);
        if (!data->lookup_table->dev_id) {
            pr_debug("Failed to allocate lookup table string");
            kfree(data->lookup_table);
            kfree(data->gpio_plat_data);
            platform_device_put(data->plat_dev);
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Allocated lookup table dev_id");
        /* replace with GPIO_LOOKUP_IDX */
        data->lookup_table->table[0].key = data->label;
        data->lookup_table->table[0].chip_hwnum = 0;
        data->lookup_table->table[0].con_id = 0;
        data->lookup_table->table[0].idx = 0;
        data->lookup_table->table[0].flags = 0;
        data->lookup_table->table[1].key = data->label;
        data->lookup_table->table[1].chip_hwnum = 1;
        data->lookup_table->table[1].con_id = 0;
        data->lookup_table->table[1].idx = 1;
        data->lookup_table->table[1].flags = 0;
        gpiod_add_lookup_table(data->lookup_table);
        pr_debug("Lookup table added");

        ret = platform_device_add(data->plat_dev);
        if (ret) {
            pr_debug("Failed to register platform device");
            platform_device_put(data->plat_dev);
            kfree(data->lookup_table);
            kfree(data->gpio_plat_data);
            kfree(data->plat_dev);
            gpiochip_remove(&data->gchip);
            kfree(data->label);
            kfree(data);
            continue;
        }
        pr_debug("Platform device registred");
        
        /* All is good, add the data to the list so we can free it later */
        pr_debug("Adding created adapter data to list");
        list_add_tail(&data->list, &qm2mod_data_list);
        data = NULL;
    }

    pr_debug("Module load");
    return 0;
}

static void __exit qm2mod_exit(void) {
    struct qm2_data *entry, *tmp;

    pr_debug("Freeing the adapter data list");
    list_for_each_entry_safe(entry, tmp, &qm2mod_data_list, list) {
        list_del(&entry->list);
        pr_debug("\tUnregistering platform device");
        platform_device_unregister(entry->plat_dev);
        pr_debug("\tFreeing lookup table");
        kfree(entry->lookup_table);
        pr_debug("\tFreeing platform data (gpio)");
        kfree(entry->gpio_plat_data);
        //pr_debug("\tFreeing platform device");
        //kfree(entry->plat_dev);
        pr_debug("\tRemoving gpio chip");
        gpiochip_remove(&entry->gchip);
        pr_debug("\tFreeing labels");
        kfree(entry->label);
        pr_debug("\tRemoving qm2 data");
        kfree(entry);
    }

    pr_debug("Module unload");
}

module_init(qm2mod_init);
module_exit(qm2mod_exit);

MODULE_DESCRIPTION("QNAP QM2 I2C driver tests");
MODULE_VERSION("0.0");
MODULE_AUTHOR("0xGiddi - <qnap8528 AT giddi.net>");
MODULE_LICENSE("GPL");