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




## ardware testing on QNAP
// Not working:
 -  - Read 0x19/0x18/0x79/0x78  depending on temp index - should be under 0x5b, otherwise over spec.



// Working

// Tested
 - Set fan PWM: set 0x20 to 0-255
- Get fan speed RPM: read 0x22 * 0x3c
- Restore default fan: set 0x24 to 0
- -  Get fan operate PWM: read 0x21

// Is 0x70 disks? or status?
// This is just setting the deafult disks to blink red
- Set ident LED: try set 0x11, 0x10, 0x71, 0x70 (usleep 1000 between) to 1 for 0, 2 for 1.

// 0 - On, 1 - Off, 2 - Blink fast, 3 - Blink slow
// 0x14 - M.2-2,  0x15- M.2-1
- Set present LED: set 0x15, 0x14, 0x75 (disk 1 2 3?) to 1 or 0
- Set active LED: 0x15, 0x14, 0x75 (disk 1 2 3?) to 2 or 0

// 0 - On, 1 - Off, 2 - Blink fast, 3 - Blink slow
// 0x10 - M.2-2,  0x11 - M.2-1
- - Set error LED: set 0x11, 0x10, 0x71 (disk 1 2 3?) 
- - Set error LED: set 0x11, 0x10, 0x71 (disk 1 2 3?) 



// Other suff
- Get PIC version: read word from 0xf2
- Get PIC mode: read word from 0xf1, result >> 8 & 0xff, result & 0xff     -  What is this?
- Get PIC flash checksum: read word from 0xfa, (crc16)











