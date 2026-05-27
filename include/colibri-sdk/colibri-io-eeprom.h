#ifndef COLIBRI_DRIVERS_IO_MANUFACTURING_H
#define COLIBRI_DRIVERS_IO_MANUFACTURING_H
#include <stdint.h>

typedef struct __attribute__((packed))
{
    uint32_t fingerprint;           // 0x0000
    uint32_t serial_number;         // 0x0004
    uint8_t reserved1[8];           // 0x0008
    uint32_t vendor_id;             // 0x0010
    uint32_t vendor_model_id;       // 0x0014
    uint32_t vendor_revision;       // 0x0018
    uint8_t reserved2[4];           // 0x001C
    uint32_t vendor_name_ptr;       // 0x0020
    uint32_t vendor_name_len;       // 0x0024
    uint32_t vendor_model_ptr;      // 0x0028
    uint32_t vendor_model_len;      // 0x002C
    uint32_t product_link_ptr;      // 0x0030
    uint32_t product_link_len;      // 0x0034
    uint32_t doc_link_ptr;          // 0x0038
    uint32_t doc_link_len;          // 0x003C
    uint32_t code_pic_len;          // 0x0040
    char reserved3[60];             // 0x0044
    uint8_t calibration_data[128];  // 0x0080
    uint8_t test_reports[256];      // 0x0100
    char text_area[2048];           // 0x0200
    uint8_t reserved4[1536];        // 0x0A00
    uint8_t vendor_area[12288];     // 0x1000
    uint8_t pic_arm[8192];          // 0x4000
} eeprom_layout_t;

#endif
