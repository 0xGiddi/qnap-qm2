
struct qm2_data {
    struct list_head list;              // 0 8 [0 1]
    struct pci_dev *pci_dev;            // 16  [2]
    char *label;                        // 24  [3]
    void __iomem *remap;                // 32  [4]
    struct gpio_chip gchip;             // 40  [5]
        // label            [5]         // 40
        // gpiodev          [6]         // 48
        // parent           [7]         // 56
        // fwnode           [8]         // 64
        // owner            [9]         // 72
        // request          [10]        // 80
        // free             [11]        // 88
        // get_direction    [12]        // 96
        // dir_inpu         [13]        // 104
        // dir_output       [14] 
        // get              [15]
        // get_mul          [16]
        // set              [17]
        // set_mul          [18]
        // set_config       [19] 
        // to_irq           [20]
        // dbg_show         [21]
        // init)valid       [22] 
        // add_pin          [23]
        // en_hw            [24]
        // dis_hw           [25]
        // 4 base           [26]
        // 2 ngpio          
        // 2 offset         
        // names            [27]
        // ? can_sleep      
        // ?valid_mask

    struct platform_device *plat_dev;   // 224
    int sda_pin;                        // 232
    int scl_pin;                        // 236
    int next_i2c_bus;                   // 240
};

