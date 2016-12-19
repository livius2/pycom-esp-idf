#pragma once

#include "esp_err.h"
#include "sdmmc_types.h"


esp_err_t sdmmc_card_init(const sdmmc_init_config_t* config,
        sdmmc_card_info_t* out_info);

esp_err_t sdmmc_write_blocks(sdmmc_card_info_t* card, const void* src,
        size_t start_sector, size_t sector_count);

esp_err_t sdmmc_read_blocks(sdmmc_card_info_t* card, void* dst,
        size_t start_sector, size_t sector_count);
