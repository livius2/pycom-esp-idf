/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Adaptations to ESP-IDF Copyright (c) 2016 Espressif Systems (Shanghai) PTE LTD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SDMMC_TYPES_H_
#define _SDMMC_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "sdmmc_defs.h"

typedef struct {
    int csd_ver;                /*!< CSD structure format */
    int mmc_ver;                /*!< MMC version (for CID format) */
    int capacity;               /*!< total number of sectors */
    int sector_size;            /*!< sector size in bytes */
    int read_block_len;         /*!< block length for reads */
    int card_command_class;     /*!< Card Command Class for SD */
    int tr_speed;               /*!< Max transfer speed */
} sdmmc_csd_t;

typedef struct {
    int mfg_id;     /*!< manufacturer identification number */
    int oem_id;     /*!< OEM/product identification number */
    char name[8];   /*!< product name (MMC v1 has the longest) */
    int revision;   /*!< product revision */
    int serial;     /*!< product serial number */
    int date;       /*!< manufacturing date */
} sdmmc_cid_t;

typedef struct {
    int sd_spec;
    int bus_width;
} sdmmc_scr_t;

typedef struct {
    uint32_t flags;
#define SDMMC_FLAG_1BIT         BIT(0)
#define SDMMC_FLAG_4BIT         BIT(1)
#define SDMMC_FLAG_8BIT         BIT(2)
    uint32_t slot;
#define SDMMC_SLOT_0    0
#define SDMMC_SLOT_1    1
    uint32_t max_freq_khz;
#define SDMMC_FREQ_DEFAULT      20000
#define SDMMC_FREQ_HIGHSPEED    40000
    float io_voltage;
} sdmmc_init_config_t;

typedef struct {
    uint32_t ocr;
    sdmmc_cid_t cid;
    sdmmc_csd_t csd;
    sdmmc_scr_t scr;
    uint16_t rca;
} sdmmc_card_info_t;

typedef uint32_t sdmmc_response_t[4];

typedef struct {
        uint32_t opcode;            /* SD or MMC command index */
        uint32_t arg;               /* SD/MMC command argument */
        sdmmc_response_t response;    /* response buffer */
        void* data;                 /* buffer to send or read into */
        size_t datalen;             /* length of data buffer */
        size_t blklen;              /* block length */
        int flags;                  /* see below */
#define SCF_ITSDONE      0x0001     /* command is complete */
#define SCF_CMD(flags)   ((flags) & 0x00f0)
#define SCF_CMD_AC       0x0000
#define SCF_CMD_ADTC     0x0010
#define SCF_CMD_BC       0x0020
#define SCF_CMD_BCR      0x0030
#define SCF_CMD_READ     0x0040     /* read command (data expected) */
#define SCF_RSP_BSY      0x0100
#define SCF_RSP_136      0x0200
#define SCF_RSP_CRC      0x0400
#define SCF_RSP_IDX      0x0800
#define SCF_RSP_PRESENT  0x1000
/* response types */
#define SCF_RSP_R0       0 /* none */
#define SCF_RSP_R1       (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R1B      (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX|SCF_RSP_BSY)
#define SCF_RSP_R2       (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_136)
#define SCF_RSP_R3       (SCF_RSP_PRESENT)
#define SCF_RSP_R4       (SCF_RSP_PRESENT)
#define SCF_RSP_R5       (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R5B      (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX|SCF_RSP_BSY)
#define SCF_RSP_R6       (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R7       (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
        esp_err_t error;            /* error returned from transfer */
} sdmmc_command_t;

/*
 * Decoded PC Card 16 based Card Information Structure (CIS),
 * per card (function 0) and per function (1 and greater).
 */
typedef struct {
        uint16_t        manufacturer;
#define SDMMC_VENDOR_INVALID    0xffff
        uint16_t        product;
#define SDMMC_PRODUCT_INVALID   0xffff
        uint8_t         function;
#define SDMMC_FUNCTION_INVALID  0xff
        uint8_t           cis1_major;
        uint8_t           cis1_minor;
        char             cis1_info_buf[256];
        char            *cis1_info[4];
} sdmmc_cis_t;




#endif // _SDMMC_TYPES_H_
