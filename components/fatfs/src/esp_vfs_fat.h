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
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/sdmmc_types.h"
#include "driver/sdmmc_host.h"
#include "ff.h"

esp_err_t esp_vfs_fat_register(const char* base_path, const char* fat_drive, size_t max_files, FATFS** out_fs);

esp_err_t esp_vfs_fat_unregister();

typedef struct {
	bool format_if_mount_failed;
	int max_files;
} esp_vfs_fat_sdmmc_mount_config_t;

esp_err_t esp_vfs_fat_sdmmc_mount(const char* base_path,
    const sdmmc_host_t* host_config,
    const sdmmc_slot_config_t* slot_config,
    const esp_vfs_fat_sdmmc_mount_config_t* mount_config);
	
esp_err_t esp_vfs_fat_sdmmc_unmount();
