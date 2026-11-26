#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

#include <stdint.h>

#define BOOT_APP_ADDR       0x1800UL
#define BOOT_APP_END        0x69FFUL
#define BOOT_FLAG_ADDR      0x6A00UL
#define BOOT_FLAG_SECTOR    0x6A00UL
#define BOOT_EEPROM_START   0x6C00UL
#define BOOT_FLASH_END      0x6FFFUL

#define BOOT_FLAG_MAGIC     0x49415055UL

#define BOOT_STATE_ERASED   0xFFFFFFFFUL
#define BOOT_STATE_REQUEST  0x5A5A0001UL
#define BOOT_STATE_WRITING  0x5A5A0002UL
#define BOOT_STATE_VALID    0x5A5A0003UL

#define BOOT_PAGE_SIZE      128U
#define BOOT_SECTOR_SIZE    512U

/* 帧：A5 5A | CMD | SEQ | LEN(2) | ADDR(2) | DATA | CRC16(2) */
#define BOOT_SOF0           0xA5
#define BOOT_SOF1           0x5A
#define BOOT_CMD_HELLO      0x01
#define BOOT_CMD_ERASE      0x02
#define BOOT_CMD_DATA       0x03
#define BOOT_CMD_END        0x04
#define BOOT_CMD_REBOOT     0x05
#define BOOT_ACK_HELLO      0x81
#define BOOT_ACK_ERASE      0x82
#define BOOT_ACK_DATA       0x83
#define BOOT_ACK_END        0x84
#define BOOT_ACK_REBOOT     0x85

#endif


