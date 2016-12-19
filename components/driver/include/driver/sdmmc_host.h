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
#include "sdmmc_types.h"
#include "driver/gpio.h"

#define SDMMC_SLOT_NO_CD      ((gpio_num_t) -1)
#define SDMMC_SLOT_NO_WP      ((gpio_num_t) -1)
#define SDMMC_HOST_SLOT_0     0
#define SDMMC_HOST_SLOT_1     1

/**
 * @brief Default initializer for sdmmc_host_t structure
 *
 * Uses SDMMC peripheral, with 4-bit mode enabled, and max frequency set to 20MHz
 */
#define SDMMC_HOST_DEFAULT() {\
    .flags = SDMMC_SLOT_FLAG_4BIT, \
    .slot = SDMMC_HOST_SLOT_1, \
    .max_freq_khz = SDMMC_FREQ_DEFAULT, \
    .io_voltage = 3.3f, \
    .set_bus_width = &sdmmc_host_set_bus_width, \
    .set_card_clk = &sdmmc_host_set_card_clk, \
    .do_transaction = &sdmmc_host_do_transaction, \
}

typedef struct {
    gpio_num_t gpio_cd;
    gpio_num_t gpio_wp;
} sdmmc_slot_config_t;

#define SDMMC_SLOT_CONFIG_DEFAULT() {\
    .gpio_cd = SDMMC_SLOT_NO_CD, \
    .gpio_wp = SDMMC_SLOT_NO_WP, \
}

esp_err_t sdmmc_host_init();

esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* slot_config);

esp_err_t sdmmc_host_set_bus_width(int slot, int width);

esp_err_t sdmmc_host_set_card_clk(int slot, uint32_t freq_khz);

esp_err_t sdmmc_host_do_transaction(int slot, sdmmc_command_t* cmdinfo);

esp_err_t sdmmc_host_deinit();

