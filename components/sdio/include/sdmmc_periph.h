// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "soc/sdmmc_struct.h"

typedef struct {
    uint32_t sdmmc_status;      ///< masked SDMMC interrupt status
    uint32_t dma_status;        ///< masked DMA interrupt status
} sdmmc_event_t;

typedef enum {
    SDMMC_PERIPH_SPEED_LOW,     ///< low speed, for initial handshake (400kHz)
    SDMMC_PERIPH_SPEED_DEFAULT, ///< default speed (up to 25MHz; currently 20MHz)
    SDMMC_PERIPH_SPEED_HIGH     ///< high speed (up to 50MHz; currently 40MHz)
} sdmmc_periph_speed_t;

void sdmmc_reset();

uint32_t sdmmc_get_hw_config();

esp_err_t sdmmc_hw_init(uint32_t max_freq_khz, QueueHandle_t event_queue);

void sdmmc_idma_prepare_transfer(sdmmc_desc_t* desc, size_t block_size, size_t data_size);

void sdmmc_idma_stop();

void sdmmc_idma_resume();

esp_err_t sdmmc_periph_set_bus_width(int width);

esp_err_t sdmmc_periph_set_speed(sdmmc_periph_speed_t speed);

void sdmmc_start_command(sdmmc_hw_cmd_t cmd, uint32_t arg);

