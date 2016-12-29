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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "diskio.h"
#include "ff.h"

static const char* hello_str = "Hello, World!\n";

#define HEAP_SIZE_CAPTURE()  \
    size_t heap_size = esp_get_free_heap_size();

#define HEAP_SIZE_CHECK(tolerance) \
    do {\
        size_t final_heap_size = esp_get_free_heap_size(); \
        if (final_heap_size < heap_size - tolerance) { \
            printf("Initial heap size: %d, final: %d, diff=%d\n", heap_size, final_heap_size, heap_size - final_heap_size); \
        } \
    } while(0)

TEST_CASE("can create and write file on sd card", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    FILE* f = fopen("/sdcard/hello.txt", "w");
    TEST_ASSERT_NOT_NULL(f);

    TEST_ASSERT_EQUAL(strlen(hello_str), fprintf(f, "%s", hello_str));
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}

TEST_CASE("can read file on sd card", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    FILE* f = fopen("/sdcard/hello.txt", "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[32];
    int cb = fread(buf, 1, sizeof(buf), f);
    TEST_ASSERT_EQUAL(strlen(hello_str), cb);
    TEST_ASSERT_EQUAL(0, strcmp(hello_str, buf));
    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}

static void speed_test(void* buf, size_t buf_size, size_t file_size, bool write)
{
    const size_t buf_count = file_size / buf_size;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = write,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    FILE* f = fopen("/sdcard/4mb.bin", (write) ? "wb" : "rb");
    TEST_ASSERT_NOT_NULL(f);

    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    for (size_t n = 0; n < buf_count; ++n) {
        if (write) {
            TEST_ASSERT_EQUAL(1, fwrite(buf, buf_size, 1, f));
        } else {
            if (fread(buf, buf_size, 1, f) != 1) {
                printf("reading at n=%d, eof=%d", n, feof(f));
                TEST_FAIL();
            }
        }
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());

    float t_s = tv_end.tv_sec - tv_start.tv_sec + 1e-6f * (tv_end.tv_usec - tv_start.tv_usec);
    printf("%s %d bytes (block size %d) in %.3fms (%.3f MB/s)\n",
            (write)?"Wrote":"Read", file_size, buf_size, t_s * 1e3,
                    (file_size / 1024 / 1024) / t_s);
}


TEST_CASE("read speed test", "[fatfs]")
{

    HEAP_SIZE_CAPTURE();
    const size_t buf_size = 16 * 1024;
    uint32_t* buf = (uint32_t*) calloc(1, buf_size);
    const size_t file_size = 4 * 1024 * 1024;
    speed_test(buf, 4 * 1024, file_size, false);
    HEAP_SIZE_CHECK(0);
    speed_test(buf, 8 * 1024, file_size, false);
    HEAP_SIZE_CHECK(0);
    speed_test(buf, 16 * 1024, file_size, false);
    HEAP_SIZE_CHECK(0);
    free(buf);
    HEAP_SIZE_CHECK(0);
}

TEST_CASE("write speed test", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();

    const size_t buf_size = 16 * 1024;
    uint32_t* buf = (uint32_t*) calloc(1, buf_size);
    for (size_t i = 0; i < buf_size / 4; ++i) {
        buf[i] = esp_random();
    }
    const size_t file_size = 4 * 1024 * 1024;

    speed_test(buf, 4 * 1024, file_size, true);
    speed_test(buf, 8 * 1024, file_size, true);
    speed_test(buf, 16 * 1024, file_size, true);

    free(buf);

    HEAP_SIZE_CHECK(0);
}

TEST_CASE("can lseek", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    FILE* f = fopen("/sdcard/seek.txt", "wb+");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(11, fprintf(f, "0123456789\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, -2, SEEK_CUR));
    TEST_ASSERT_EQUAL('9', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 3, SEEK_SET));
    TEST_ASSERT_EQUAL('3', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, -3, SEEK_END));
    TEST_ASSERT_EQUAL('8', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 3, SEEK_END));
    TEST_ASSERT_EQUAL(14, ftell(f));
    TEST_ASSERT_EQUAL(4, fprintf(f, "abc\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    TEST_ASSERT_EQUAL(18, ftell(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
    char buf[20];
    TEST_ASSERT_EQUAL(18, fread(buf, 1, sizeof(buf), f));
    const char ref_buf[] = "0123456789\n\0\0\0abc\n";
    TEST_ASSERT_EQUAL_INT8_ARRAY(ref_buf, buf, sizeof(ref_buf) - 1);

    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}

TEST_CASE("stat returns correct values", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    struct tm tm;
    tm.tm_year = 2016 - 1900;
    tm.tm_mon = 0;
    tm.tm_mday = 10;
    tm.tm_hour = 16;
    tm.tm_min = 30;
    tm.tm_sec = 0;
    printf("Setting time: %s", asctime(&tm));
    time_t t = mktime(&tm);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);

    FILE* f = fopen("/sdcard/stat.txt", "w+");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_EQUAL(EOF, fputs("foo\n", f));
    TEST_ASSERT_EQUAL(0, fclose(f));

    struct stat st;
    TEST_ASSERT_EQUAL(0, stat("/sdcard/stat.txt", &st));
    time_t mtime = st.st_mtime;
    struct tm mtm;
    localtime_r(&mtime, &mtm);
    printf("File time: %s", asctime(&mtm));
    TEST_ASSERT(abs(mtime - t) < 2);    // fatfs library stores time with 2 second precision

    TEST_ASSERT(st.st_mode & S_IFREG);
    TEST_ASSERT_FALSE(st.st_mode & S_IFDIR);

    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}

TEST_CASE("unlink removes a file", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    FILE* f = fopen("/sdcard/unlink.txt", "w+");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_EQUAL(EOF, fputs("unlink\n", f));
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, unlink("/sdcard/unlink.txt"));

    TEST_ASSERT_NULL(fopen("/sdcard/unlink.txt", "r"));

    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}

TEST_CASE("link copies a file, rename moves a file", "[fatfs]")
{
    HEAP_SIZE_CAPTURE();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5
    };
    TEST_ESP_OK(esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config));

    unlink("/sdcard/linkcopy.txt");
    unlink("/sdcard/link_dst.txt");
    unlink("/sdcard/link_src.txt");

    FILE* f = fopen("/sdcard/link_src.txt", "w+");
    TEST_ASSERT_NOT_NULL(f);
    char* str = "0123456789";
    for (int i = 0; i < 4000; ++i) {
        TEST_ASSERT_NOT_EQUAL(EOF, fputs(str, f));
    }
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, link("/sdcard/link_src.txt", "/sdcard/linkcopy.txt"));

    FILE* fcopy = fopen("/sdcard/linkcopy.txt", "r");
    TEST_ASSERT_NOT_NULL(fcopy);
    TEST_ASSERT_EQUAL(0, fseek(fcopy, 0, SEEK_END));
    TEST_ASSERT_EQUAL(40000, ftell(fcopy));
    TEST_ASSERT_EQUAL(0, fclose(fcopy));

    TEST_ASSERT_EQUAL(0, rename("/sdcard/linkcopy.txt", "/sdcard/link_dst.txt"));
    TEST_ASSERT_NULL(fopen("/sdcard/linkcopy.txt", "r"));
    FILE* fdst = fopen("/sdcard/link_dst.txt", "r");
    TEST_ASSERT_NOT_NULL(fdst);
    TEST_ASSERT_EQUAL(0, fseek(fdst, 0, SEEK_END));
    TEST_ASSERT_EQUAL(40000, ftell(fdst));
    TEST_ASSERT_EQUAL(0, fclose(fdst));

    TEST_ESP_OK(esp_vfs_fat_sdmmc_unmount());
    HEAP_SIZE_CHECK(0);
}
