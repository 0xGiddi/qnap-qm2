# Hardware

 - ASM2824 PCI Bridge
 - Marvell aqc113c ethernet controller
 - PIC16F105 MCU
 - Card has 2 SPI flash chips, dumped 25L512, does not contain a serial or the device model, possible configuration for the ASM2824? Has string "ASMT"
 - Second flash chip did not dump, remove from PCB? after hardware  testing.
 - Possible ICD/ICP pins on edge
  

# Reverse engineering 

## From qm2-i2c.ko
- Interface seems to be on I2C (except for QM2-2S10G1TB02, Aquantia Corp. cards, subsysid 1baa:(07b2/1))
- I2C is via memory mapping 


## From libuLinux_hal.so










