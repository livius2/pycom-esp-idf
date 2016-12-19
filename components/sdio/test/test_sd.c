#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "sdmmc_req.h"
#include "esp_log.h"
#include "esp_heap_alloc_caps.h"



static void print_card_info(const sdmmc_card_info_t* card) {
    printf("Card name: %s\n", card->cid.name);
    printf("Card CSD: ver=%d, flags=%x, sector_size=%d, capacity=%d read_bl_len=%d\n",
            card->csd.csd_ver, card->ocr & SD_OCR_SDHC_CAP,
            card->csd.sector_size, card->csd.capacity, card->csd.read_block_len);
    printf("SCR: sd_spec=%d, bus_width=%d\n", card->scr.sd_spec, card->scr.bus_width);
}

TEST_CASE("can probe SD", "[sd]")
{
    sdmmc_req_init();
    esp_log_level_set("sdmmc_req", ESP_LOG_DEBUG);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_INFO);
    sdmmc_init_config_t config = {
            .flags = SDMMC_FLAG_1BIT,
            .slot = SDMMC_SLOT_1,
            .max_freq_khz = SDMMC_FREQ_DEFAULT,
            .io_voltage = 3.3f
    };
    sdmmc_card_info_t* card = malloc(sizeof(sdmmc_card_info_t));
    TEST_ASSERT_NOT_NULL(card);
    TEST_ESP_OK(sdmmc_card_init(&config, card));
    print_card_info(card);
    sdmmc_req_deinit();
    free(card);
}

static void do_single_write_read_test(sdmmc_card_info_t* card,
        size_t start_block, size_t block_count)
{
    printf("r/w test: start=%d count=%d\n", start_block, block_count);
    size_t block_size = card->csd.sector_size;
    size_t total_size = block_size * block_count;
    uint32_t* buffer = pvPortMallocCaps(total_size, MALLOC_CAP_DMA);
    srand(start_block);
    for (size_t i = 0; i < total_size / sizeof(buffer[0]); ++i) {
        buffer[i] = rand();
    }
    TEST_ESP_OK(sdmmc_write_blocks(card, buffer, start_block, block_count));
    memset(buffer, 0xbb, total_size);
    TEST_ESP_OK(sdmmc_read_blocks(card, buffer, start_block, block_count));
    srand(start_block);
    for (size_t i = 0; i < total_size / sizeof(buffer[0]); ++i) {
        TEST_ASSERT_EQUAL_HEX32(rand(), buffer[i]);
    }
    free(buffer);
}

TEST_CASE("can write and read back blocks", "[sd]")
{
    sdmmc_req_init();
    gpio_pullup_en(12);
    gpio_pullup_en(13);
    esp_log_level_set("sdmmc_req", ESP_LOG_INFO);
    esp_log_level_set("sdmmc_cmd", ESP_LOG_DEBUG);
    sdmmc_init_config_t config = {
            .flags = SDMMC_FLAG_4BIT,
            .slot = SDMMC_SLOT_1,
            .max_freq_khz = 400,
            .io_voltage = 3.3f
    };
    sdmmc_card_info_t* card = malloc(sizeof(sdmmc_card_info_t));
    TEST_ASSERT_NOT_NULL(card);
    TEST_ESP_OK(sdmmc_card_init(&config, card));
    print_card_info(card);
    do_single_write_read_test(card, 0, 1);
    do_single_write_read_test(card, 0, 4);
    do_single_write_read_test(card, 1, 16);
    do_single_write_read_test(card, 16, 32);
    do_single_write_read_test(card, 48, 64);
    do_single_write_read_test(card, card->csd.capacity - 64, 32);
    do_single_write_read_test(card, card->csd.capacity - 8, 1);
    do_single_write_read_test(card, card->csd.capacity/2, 1);
    do_single_write_read_test(card, card->csd.capacity/2, 4);
    do_single_write_read_test(card, card->csd.capacity/2, 8);
    do_single_write_read_test(card, card->csd.capacity/2, 16);
    do_single_write_read_test(card, card->csd.capacity/2, 32);
    do_single_write_read_test(card, card->csd.capacity/2, 64);
    free(card);
    sdmmc_req_deinit();
}
