#ifndef COLIBRI_RUNTIME_EEPROM_H
#define COLIBRI_RUNTIME_EEPROM_H
#include <stdint.h>

typedef struct
{
    uint32_t serial_number;         // 0x0004
    uint32_t vendor_id;             // 0x0008
    uint32_t model_id;              // 0x000C
    uint32_t vendor_model_ptr;      // 0x0010
    uint32_t vendor_revision;       // 0x0014
    uint32_t vendor_name_ptr;       // 0x0018
    uint32_t vendor_name_len;       // 0x001C
    uint32_t model_name_ptr;        // 0x0020
    uint32_t model_name_len;        // 0x0024
    uint32_t product_link_ptr;      // 0x0028
    uint32_t product_link_len;      // 0x002C
    uint32_t doc_link_ptr;          // 0x0030
    uint32_t doc_link_len;          // 0x0034
    uint32_t code_pic_len;          // 0x0038
} eeprom_layout;

#endif
