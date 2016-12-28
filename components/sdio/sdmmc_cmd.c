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

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_alloc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_periph.h"
#include "sdmmc_cmd.h"
#include "sdmmc_req.h"
#include "sdmmc_defs.h"
#include "sdmmc_types.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

// #define printf(...)

// static const char* TAG = "sdmmc_cmd";

static esp_err_t sdmmc_send_cmd(sdmmc_command_t* cmd);
static esp_err_t sdmmc_send_app_cmd(uint16_t rca, sdmmc_command_t* cmd);
static esp_err_t sdmmc_send_cmd_go_idle_state();
static esp_err_t sdmmc_send_cmd_send_if_cond(uint32_t card_ocr);
static esp_err_t sdmmc_send_cmd_send_op_cond(uint32_t ocr, uint32_t *ocrp);
static esp_err_t sdmmc_decode_cid(sdmmc_response_t resp, sdmmc_cid_t* out_cid);
static esp_err_t sddmc_send_cmd_all_send_cid(sdmmc_cid_t* out_cid);
static esp_err_t sdmmc_send_cmd_set_relative_addr(uint16_t* out_rca);
static esp_err_t sdmmc_send_cmd_set_blocklen(sdmmc_csd_t* csd);
static esp_err_t sdmmc_decode_csd(sdmmc_response_t response, sdmmc_csd_t* out_csd);
static esp_err_t sdmmc_send_cmd_send_csd(uint16_t rca, sdmmc_csd_t* out_csd);
static esp_err_t sdmmc_send_cmd_select_card(uint16_t rca);
static esp_err_t sdmmc_decode_scr(uint32_t *raw_scr, sdmmc_scr_t* out_scr);
static esp_err_t sdmmc_send_cmd_send_scr(uint16_t rca, sdmmc_scr_t *out_scr);
static esp_err_t sdmmc_send_cmd_set_bus_width(uint16_t rca, int width);
static esp_err_t sdmmc_send_cmd_stop_transmission(uint32_t* status);
static esp_err_t sdmmc_send_cmd_send_status(uint16_t rca, uint32_t* out_status);
static uint32_t  get_host_ocr(float voltage);



esp_err_t sdmmc_card_init(const sdmmc_init_config_t* config,
        sdmmc_card_info_t* out_info)
{
    if (config->max_freq_khz > 40000) {
        printf( "frequency exceeds max supported by driver\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->io_voltage < 2.8f || config->io_voltage > 3.6f) {
        printf( "IO voltage is not supported by driver\n");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sdmmc_send_cmd_go_idle_state();
    if (err != ESP_OK) {
        printf( "%s: go_idle_state (1) returned %d\n", __func__, err);
        return err;
    }
    uint32_t card_ocr;
    err = sdmmc_send_cmd_send_op_cond(0, &card_ocr);
    if (err != ESP_OK) {
        printf( "%s: send_op_cond (1) returned %d\n", __func__, err);
        return err;
    }
    uint32_t host_ocr = get_host_ocr(config->io_voltage);
    if ((host_ocr & card_ocr) == 0) {
        printf( "%s: card reported unsupported OCR: %08x\n", __func__, card_ocr);
        return err;
    }
    err = sdmmc_send_cmd_go_idle_state();
    if (err != ESP_OK) {
        printf( "%s: go_idle_state (2) returned %d\n", __func__, err);
        return err;
    }
    err = sdmmc_send_cmd_send_if_cond(card_ocr);
    if (err != ESP_OK) {
        printf( "%s: send_if_cond returned %d\n", __func__, err);
        return err;
    }
    host_ocr &= card_ocr;
    host_ocr |= SD_OCR_SDHC_CAP;
    printf( "sdmmc_card_init: host_ocr=%08x, card_ocr=%08x\n", host_ocr, card_ocr);
    err = sdmmc_send_cmd_send_op_cond(host_ocr, NULL);
    if (err != ESP_OK) {
        printf( "%s: send_op_cond (2) returned %d\n", __func__, err);
        return err;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->ocr = card_ocr;
    err = sddmc_send_cmd_all_send_cid(&out_info->cid);
    if (err != ESP_OK) {
        printf( "%s: all_send_cid returned %d\n", __func__, err);
        return err;
    }
    err = sdmmc_send_cmd_set_relative_addr(&out_info->rca);
    if (err != ESP_OK) {
        printf( "%s: set_relative_addr returned %d\n", __func__, err);
        return err;
    }
    err = sdmmc_send_cmd_send_csd(out_info->rca, &out_info->csd);
    if (err != ESP_OK) {
        printf( "%s: send_csd returned %d\n", __func__, err);
        return err;
    }
    err = sdmmc_send_cmd_select_card(out_info->rca);
    if (err != ESP_OK) {
        printf( "%s: select_card returned %d\n", __func__, err);
        return err;
    }
    if ((out_info->ocr & SD_OCR_SDHC_CAP) == 0) {
        err = sdmmc_send_cmd_set_blocklen(&out_info->csd);
        if (err != ESP_OK) {
            printf( "%s: set_blocklen returned %d\n", __func__, err);
            return err;
        }
    }
    err = sdmmc_send_cmd_send_scr(out_info->rca, &out_info->scr);
    if (err != ESP_OK) {
        printf( "%s: send_scr returned %d\n", __func__, err);
        return err;
    }
    if ((config->flags & SDMMC_FLAG_4BIT) &&
        (out_info->scr.bus_width & SCR_SD_BUS_WIDTHS_4BIT)) {
        printf( "switching to 4-bit bus mode");
        err = sdmmc_send_cmd_set_bus_width(out_info->rca, 4);
        if (err != ESP_OK) {
            printf( "set_bus_width failed");
            return err;
        }
        uint32_t status;
        err = sdmmc_send_cmd_stop_transmission(&status);
        if (err != ESP_OK) {
            printf( "stop_transmission failed, status=%d\n", status);
            return err;
        }
    }
    uint32_t status = 0;
    while (!(status & MMC_R1_READY_FOR_DATA)) {
        // TODO: add some timeout here
        uint32_t count = 0;
        err = sdmmc_send_cmd_send_status(out_info->rca, &status);
        if (err != ESP_OK) {
            return err;
        }
        if (++count % 10 == 0) {
            printf( "waiting for card to become ready (%d)\n", count);
        }
    }
    if (config->max_freq_khz >= SDMMC_FREQ_HIGHSPEED &&
        out_info->csd.tr_speed / 1000 > SDMMC_FREQ_HIGHSPEED) {
        printf( "switching to HS bus mode\n");
        sdmmc_periph_set_speed(SDMMC_PERIPH_SPEED_HIGH);
    } else if (config->max_freq_khz >= SDMMC_FREQ_DEFAULT &&
        out_info->csd.tr_speed / 1000 > SDMMC_FREQ_DEFAULT) {
        printf( "switching to DS bus mode\n");
        sdmmc_periph_set_speed(SDMMC_PERIPH_SPEED_DEFAULT);
    }
    sdmmc_scr_t scr_tmp;
    err = sdmmc_send_cmd_send_scr(out_info->rca, &scr_tmp);
    if (err != ESP_OK) {
        printf( "%s: send_scr returned %d\n", __func__, err);
        return err;
    }
    if (memcmp(&out_info->scr, &scr_tmp, sizeof(scr_tmp)) != 0) {
        printf( "data check fail!\n");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}


static esp_err_t sdmmc_send_cmd(sdmmc_command_t* cmd)
{
    printf( "sending cmd op=%d arg=%x flags=%x data=%p blklen=%d datalen=%d\n",
            cmd->opcode, cmd->arg, cmd->flags, cmd->data, cmd->blklen, cmd->datalen);
    esp_err_t err = sdmmc_req_run(cmd);
    if (err != 0) {
        printf( "sdmmc_req_run returned %d", err);
        return err;
    }
    int state = MMC_R1_CURRENT_STATE(cmd->response);
    printf( "cmd response %08x %08x %08x %08x err=%d state=%d\n",
               cmd->response[0],
               cmd->response[1],
               cmd->response[2],
               cmd->response[3],
               cmd->error,
               state);
    return cmd->error;
}

static esp_err_t sdmmc_send_app_cmd(uint16_t rca, sdmmc_command_t* cmd)
{
    sdmmc_command_t app_cmd = {
        .opcode = MMC_APP_CMD,
        .flags = SCF_CMD_AC | SCF_RSP_R1,
        .arg = MMC_ARG_RCA(rca)
    };
    esp_err_t err = sdmmc_send_cmd(&app_cmd);
    if (err != ESP_OK) {
        printf("error sending APP_CMD\n");
        return err;
    }
    if (!(MMC_R1(app_cmd.response) & MMC_R1_APP_CMD)) {
        printf( "card doesn't support APP_CMD\n");
        return ESP_ERR_NOT_SUPPORTED;
    }
    return sdmmc_send_cmd(cmd);
}


static esp_err_t sdmmc_send_cmd_go_idle_state()
{
    sdmmc_command_t cmd = {
        .opcode = MMC_GO_IDLE_STATE,
        .flags = SCF_CMD_BC | SCF_RSP_R0
    };
    return sdmmc_send_cmd(&cmd);
}


static esp_err_t sdmmc_send_cmd_send_if_cond(uint32_t card_ocr)
{
    const uint8_t pattern = 0x23; /* any pattern will do here */
    sdmmc_command_t cmd = {
        .opcode = SD_SEND_IF_COND,
        .arg = ((card_ocr & SD_OCR_VOL_MASK) != 0) << 8 | pattern,
        .flags = SCF_CMD_BCR | SCF_RSP_R7
    };
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t response = cmd.response[0] & 0xff;
    if (response != pattern) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t sdmmc_send_cmd_send_op_cond(uint32_t ocr, uint32_t *ocrp)
{
    sdmmc_command_t cmd = {
            .arg = ocr,
            .flags = SCF_CMD_BCR | SCF_RSP_R3,
            .opcode = SD_APP_OP_COND
    };
    int nretries = 100;   // arbitrary, BSD driver uses this value
    for (; nretries != 0; --nretries)  {
        esp_err_t err = sdmmc_send_app_cmd(0, &cmd);
        if (err != ESP_OK) {
            return err;
        }
        if ((MMC_R3(cmd.response) & MMC_OCR_MEM_READY) ||
             ocr == 0) {
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (nretries == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (ocrp) {
        *ocrp = MMC_R3(cmd.response);
    }
    return ESP_OK;
}

static esp_err_t sdmmc_decode_cid(sdmmc_response_t resp, sdmmc_cid_t* out_cid)
{
    out_cid->mfg_id = SD_CID_MID(resp);
    out_cid->oem_id = SD_CID_OID(resp);
    SD_CID_PNM_CPY(resp, out_cid->name);
    out_cid->revision = SD_CID_REV(resp);
    out_cid->serial = SD_CID_PSN(resp);
    out_cid->date = SD_CID_MDT(resp);
    return ESP_OK;
}

static esp_err_t sddmc_send_cmd_all_send_cid(sdmmc_cid_t* out_cid)
{
    assert(out_cid);
    sdmmc_command_t cmd = {
            .opcode = MMC_ALL_SEND_CID,
            .flags = SCF_CMD_BCR | SCF_RSP_R2
    };
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    return sdmmc_decode_cid(cmd.response, out_cid);
}


static esp_err_t sdmmc_send_cmd_set_relative_addr(uint16_t* out_rca)
{
    assert(out_rca);
    sdmmc_command_t cmd = {
            .opcode = SD_SEND_RELATIVE_ADDR,
            .flags = SCF_CMD_BCR | SCF_RSP_R6
    };

    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    *out_rca = SD_R6_RCA(cmd.response);
    return ESP_OK;
}


static esp_err_t sdmmc_send_cmd_set_blocklen(sdmmc_csd_t* csd)
{
    sdmmc_command_t cmd = {
            .opcode = MMC_SET_BLOCKLEN,
            .arg = csd->sector_size,
            .flags = SCF_CMD_AC | SCF_RSP_R1
    };
    return sdmmc_send_cmd(&cmd);
}

static esp_err_t sdmmc_decode_csd(sdmmc_response_t response, sdmmc_csd_t* out_csd)
{
    out_csd->csd_ver = SD_CSD_CSDVER(response);
    switch (out_csd->csd_ver) {
    case SD_CSD_CSDVER_2_0:
        out_csd->capacity = SD_CSD_V2_CAPACITY(response);
        out_csd->read_block_len = SD_CSD_V2_BL_LEN;
        break;
    case SD_CSD_CSDVER_1_0:
        out_csd->capacity = SD_CSD_CAPACITY(response);
        out_csd->read_block_len = SD_CSD_READ_BL_LEN(response);
        break;
    default:
        printf( "unknown SD CSD structure version 0x%x\n", out_csd->csd_ver);
        return ESP_ERR_NOT_SUPPORTED;
    }
    out_csd->card_command_class = SD_CSD_CCC(response);
    int read_bl_size = 1 << out_csd->read_block_len;
    out_csd->sector_size = MIN(read_bl_size, 512);
    if (out_csd->sector_size < read_bl_size) {
        out_csd->capacity *= read_bl_size / out_csd->sector_size;
    }
    int speed = SD_CSD_SPEED(response);
    if (speed == SD_CSD_SPEED_50_MHZ) {
        out_csd->tr_speed = 50000000;
    } else {
        out_csd->tr_speed = 25000000;
    }
    return ESP_OK;
}

static esp_err_t sdmmc_send_cmd_send_csd(uint16_t rca, sdmmc_csd_t* out_csd)
{
    sdmmc_command_t cmd = {
            .opcode = MMC_SEND_CSD,
            .arg = MMC_ARG_RCA(rca),
            .flags = SCF_CMD_AC | SCF_RSP_R2
    };
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    return sdmmc_decode_csd(cmd.response, out_csd);
}

static esp_err_t sdmmc_send_cmd_select_card(uint16_t rca)
{
    sdmmc_command_t cmd = {
            .opcode = MMC_SELECT_CARD,
            .arg = MMC_ARG_RCA(rca),
            .flags = SCF_CMD_AC | SCF_RSP_R1
    };
    return sdmmc_send_cmd(&cmd);
}

static esp_err_t sdmmc_decode_scr(uint32_t *raw_scr, sdmmc_scr_t* out_scr)
{
    sdmmc_response_t resp = {0xabababab, 0xabababab, 0x12345678, 0x09abcdef};
    resp[2] = __builtin_bswap32(raw_scr[0]);
    resp[3] = __builtin_bswap32(raw_scr[1]);
    int ver = SCR_STRUCTURE(resp);
    if (ver != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    out_scr->sd_spec = SCR_SD_SPEC(resp);
    out_scr->bus_width = SCR_SD_BUS_WIDTHS(resp);
    return ESP_OK;
}

static esp_err_t sdmmc_send_cmd_send_scr(uint16_t rca, sdmmc_scr_t *out_scr)
{
    size_t datalen = 8;
    uint32_t* buf = (uint32_t*) pvPortMallocCaps(datalen, MALLOC_CAP_DMA);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    sdmmc_command_t cmd = {
            .data = buf,
            .datalen = datalen,
            .blklen = datalen,
            .flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1,
            .opcode = SD_APP_SEND_SCR
    };
    esp_err_t err = sdmmc_send_app_cmd(rca, &cmd);
    if (err == ESP_OK) {
        buf[0] = (buf[0]);
        buf[1] = (buf[1]);
        err = sdmmc_decode_scr(buf, out_scr);
    }
    free(buf);
    return err;
}

static esp_err_t sdmmc_send_cmd_set_bus_width(uint16_t rca, int width)
{
    sdmmc_command_t cmd = {
            .opcode = SD_APP_SET_BUS_WIDTH,
            .flags = SCF_RSP_R1 | SCF_CMD_AC,
            .arg = (width == 4) ? SD_ARG_BUS_WIDTH_4 : SD_ARG_BUS_WIDTH_1
    };

    esp_err_t err = sdmmc_send_app_cmd(rca, &cmd);
    if (err == ESP_OK) {
        err = sdmmc_periph_set_bus_width(width);
    }
    return err;
}

static esp_err_t sdmmc_send_cmd_stop_transmission(uint32_t* status)
{
    sdmmc_command_t cmd = {
            .opcode = MMC_STOP_TRANSMISSION,
            .arg = 0,
            .flags = SCF_RSP_R1B | SCF_CMD_AC
    };
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err == 0) {
        *status = MMC_R1(cmd.response);
    }
    return err;
}

static uint32_t get_host_ocr(float voltage)
{
    // TODO: report exact voltage to the card
    // For now tell that the host has 2.8-3.6V voltage range
    (void) voltage;
    return SD_OCR_VOL_MASK;
}

static esp_err_t sdmmc_send_cmd_send_status(uint16_t rca, uint32_t* out_status)
{
    sdmmc_command_t cmd = {
            .opcode = MMC_SEND_STATUS,
            .arg = MMC_ARG_RCA(rca),
            .flags = SCF_CMD_AC | SCF_RSP_R1
    };
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        return err;
    }
    if (out_status) {
        *out_status = MMC_R1(cmd.response);
    }
    return ESP_OK;
}

esp_err_t sdmmc_write_blocks(sdmmc_card_info_t* card, const void* src,
        size_t start_block, size_t block_count)
{
    size_t block_size = card->csd.sector_size;
    sdmmc_command_t cmd = {
            .flags = SCF_CMD_ADTC | SCF_RSP_R1,
            .blklen = block_size,
            .data = (void*) src,
            .datalen = block_count * block_size
    };
    if (block_count == 1) {
        cmd.opcode = MMC_WRITE_BLOCK_SINGLE;
    } else {
        cmd.opcode = MMC_WRITE_BLOCK_MULTIPLE;
    }
    if (card->ocr & SD_OCR_SDHC_CAP) {
        cmd.arg = start_block;
    } else {
        cmd.arg = start_block * block_size;
    }
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        printf( "%s: sdmmc_send_cmd returned %d\n", __func__, err);
        return err;
    }
    uint32_t status = 0;
    size_t count = 0;
    while (!(status & MMC_R1_READY_FOR_DATA)) {
        // TODO: add some timeout here
        err = sdmmc_send_cmd_send_status(card->rca, &status);
        if (err != ESP_OK) {
            return err;
        }
        if (++count % 10 == 0) {
            printf( "waiting for card to become ready (%d)\n", count);
        }
    }
    return ESP_OK;
}

esp_err_t sdmmc_read_blocks(sdmmc_card_info_t* card, void* dst,
        size_t start_block, size_t block_count)
{
    size_t block_size = card->csd.sector_size;
    sdmmc_command_t cmd = {
            .flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1,
            .blklen = block_size,
            .data = (void*) dst,
            .datalen = block_count * block_size
    };
    if (block_count == 1) {
        cmd.opcode = MMC_READ_BLOCK_SINGLE;
    } else {
        cmd.opcode = MMC_READ_BLOCK_MULTIPLE;
    }
    if (card->ocr & SD_OCR_SDHC_CAP) {
        cmd.arg = start_block;
    } else {
        cmd.arg = start_block * block_size;
    }
    esp_err_t err = sdmmc_send_cmd(&cmd);
    if (err != ESP_OK) {
        printf( "%s: sdmmc_send_cmd returned %d\n", __func__, err);
        return err;
    }
    uint32_t status = 0;
    size_t count = 0;
    while (!(status & MMC_R1_READY_FOR_DATA)) {
        // TODO: add some timeout here
        err = sdmmc_send_cmd_send_status(card->rca, &status);
        if (err != ESP_OK) {
            return err;
        }
        if (++count % 10 == 0) {
            printf( "waiting for card to become ready (%d)\n", count);
        }
    }
    return ESP_OK;
}
