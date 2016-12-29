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

#include <string.h>
#include <stdlib.h>
#include "ff.h"
#include "diskio.h"
#include "esp_vfs.h"
#include "esp_log.h"
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <unistd.h>

typedef struct {
    char fat_drive[8];
    size_t max_files;
    FATFS fs;
    FIL files[0];
} vfs_fat_ctx_t;

static const char* TAG = "vfs_fat";

static size_t vfs_fat_write(void* p, int fd, const void * data, size_t size);
static off_t vfs_fat_lseek(void* p, int fd, off_t size, int mode);
static ssize_t vfs_fat_read(void* ctx, int fd, void * dst, size_t size);
static int vfs_fat_open(void* ctx, const char * path, int flags, int mode);
static int vfs_fat_close(void* ctx, int fd);
static int vfs_fat_fstat(void* ctx, int fd, struct stat * st);
static int vfs_fat_stat(void* ctx, const char * path, struct stat * st);
static int vfs_fat_link(void* ctx, const char* n1, const char* n2);
static int vfs_fat_unlink(void* ctx, const char *path);
static int vfs_fat_rename(void* ctx, const char *src, const char *dst);

static char s_base_path[ESP_VFS_PATH_MAX];
static vfs_fat_ctx_t* s_fat_ctx = NULL;

esp_err_t esp_vfs_fat_register(const char* base_path, const char* fat_drive, size_t max_files, FATFS** out_fs)
{
    if (s_fat_ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p = &vfs_fat_write,
        .lseek_p = &vfs_fat_lseek,
        .read_p = &vfs_fat_read,
        .open_p = &vfs_fat_open,
        .close_p = &vfs_fat_close,
        .fstat_p = &vfs_fat_fstat,
        .stat_p = &vfs_fat_stat,
        .link_p = &vfs_fat_link,
        .unlink_p = &vfs_fat_unlink,
        .rename_p = &vfs_fat_rename
    };
    size_t ctx_size = sizeof(vfs_fat_ctx_t) + max_files * sizeof(FIL);
    s_fat_ctx = (vfs_fat_ctx_t*) calloc(1, ctx_size);
    if (s_fat_ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_fat_ctx->max_files = max_files;
    strncpy(s_fat_ctx->fat_drive, fat_drive, sizeof(s_fat_ctx->fat_drive) - 1);
    *out_fs = &s_fat_ctx->fs;
    esp_err_t err = esp_vfs_register(base_path, &vfs, s_fat_ctx);
    if (err != ESP_OK) {
        free(s_fat_ctx);
        s_fat_ctx = NULL;
        return err;
    }
    strncpy(s_base_path, base_path, sizeof(s_base_path) - 1);
    s_base_path[sizeof(s_base_path) - 1] = 0;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_unregister()
{
    if (s_fat_ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_vfs_unregister(s_base_path);
    if (err != ESP_OK) {
        return err;
    }
    free(s_fat_ctx);
    s_fat_ctx = NULL;
    return ESP_OK;
}

static int get_next_fd(vfs_fat_ctx_t* fat_ctx)
{
    for (size_t i = 0; i < fat_ctx->max_files; ++i) {
        if (fat_ctx->files[i].obj.fs == NULL) {
            return (int) i;
        }
    }
    return -1;
}

static int fat_mode_conv(int m)
{
    int res = 0;
    int acc_mode = m & O_ACCMODE;
    if (acc_mode == O_RDONLY) {
        res |= FA_READ;
    } else if (acc_mode == O_WRONLY) {
        res |= FA_WRITE;
    } else if (acc_mode == O_RDWR) {
        res |= FA_READ | FA_WRITE;
    }
    if ((m & O_CREAT) && (m & O_EXCL)) {
        res |= FA_CREATE_NEW;
    } else if (m & O_CREAT) {
        res |= FA_CREATE_ALWAYS;
    } else if (m & O_APPEND) {
        res |= FA_OPEN_ALWAYS;
    } else {
        res |= FA_OPEN_EXISTING;
    }
    return res;
}

static int fresult_to_errno(FRESULT fr)
{
    switch(fr) {
        case FR_DISK_ERR:       return EIO;
        case FR_INT_ERR:
            assert(0 && "fatfs internal error");
            return EIO;
        case FR_NOT_READY:      return ENODEV;
        case FR_NO_FILE:        return ENOENT;
        case FR_NO_PATH:        return ENOENT;
        case FR_INVALID_NAME:   return EINVAL;
        case FR_DENIED:         return EACCES;
        case FR_EXIST:          return EEXIST;
        case FR_INVALID_OBJECT: return EBADF;
        case FR_WRITE_PROTECTED: return EACCES;
        case FR_INVALID_DRIVE:  return ENXIO;
        case FR_NOT_ENABLED:    return ENODEV;
        case FR_NO_FILESYSTEM:  return ENODEV;
        case FR_MKFS_ABORTED:   return EINTR;
        case FR_TIMEOUT:        return ETIMEDOUT;
        case FR_LOCKED:         return EACCES;
        case FR_NOT_ENOUGH_CORE: return ENOMEM;
        case FR_TOO_MANY_OPEN_FILES: return ENFILE;
        case FR_INVALID_PARAMETER: return EINVAL;
        case FR_OK: return 0;
    }
    assert(0 && "unhandled FRESULT");
    return ENOTSUP;
}

static void file_cleanup(vfs_fat_ctx_t* ctx, int fd)
{
    memset(&ctx->files[fd], 0, sizeof(FIL));
}

static int vfs_fat_open(void* ctx, const char * path, int flags, int mode)
{
    ESP_LOGV(TAG, "%s: path=\"%s\", flags=%x, mode=%x", __func__, path, flags, mode);
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    int fd = get_next_fd(fat_ctx);
    if (fd < 0) {
        ESP_LOGE(TAG, "open: no free file descriptors");
        errno = ENFILE;
        return -1;
    }
    FRESULT res = f_open(&fat_ctx->files[fd], path, fat_mode_conv(flags));
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        file_cleanup(fat_ctx, fd);
        errno = fresult_to_errno(res);
        return -1;
    }
    return fd;
}

static size_t vfs_fat_write(void* ctx, int fd, const void * data, size_t size)
{
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    FIL* file = &fat_ctx->files[fd];
    unsigned written = 0;
    ESP_LOGD(TAG, "fwrite: size=%d", size);
    FRESULT res = f_write(file, data, size, &written);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        if (written == 0) {
            return -1;
        }
    }
    return written;
}

static ssize_t vfs_fat_read(void* ctx, int fd, void * dst, size_t size)
{
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    FIL* file = &fat_ctx->files[fd];
    unsigned read = 0;
    FRESULT res = f_read(file, dst, size, &read);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        if (read == 0) {
            return -1;
        }
    }
    return read;
}

static int vfs_fat_close(void* ctx, int fd)
{
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    FIL* file = &fat_ctx->files[fd];
    FRESULT res = f_close(file);
    file_cleanup(fat_ctx, fd);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    return 0;
}

static off_t vfs_fat_lseek(void* ctx, int fd, off_t offset, int mode)
{
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    FIL* file = &fat_ctx->files[fd];
    off_t new_pos;
    if (mode == SEEK_SET) {
        new_pos = offset;
    } else if (mode == SEEK_CUR) {
        off_t cur_pos = f_tell(file);
        new_pos = cur_pos + offset;
    } else if (mode == SEEK_END) {
        off_t size = f_size(file);
        new_pos = size + offset;
    } else {
        errno = EINVAL;
        return -1;
    }
    FRESULT res = f_lseek(file, new_pos);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    return new_pos;
}

static int vfs_fat_fstat(void* ctx, int fd, struct stat * st)
{
    vfs_fat_ctx_t* fat_ctx = (vfs_fat_ctx_t*) ctx;
    FIL* file = &fat_ctx->files[fd];
    st->st_size = f_size(file);
    st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFREG;
    return 0;
}

static int vfs_fat_stat(void* ctx, const char * path, struct stat * st)
{
    FILINFO info;
    FRESULT res = f_stat(path, &info);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    st->st_size = info.fsize;
    st->st_mode = S_IRWXU | S_IRWXG | S_IRWXO |
            ((info.fattrib & AM_DIR) ? S_IFDIR : S_IFREG);
    struct tm tm;
    uint16_t fdate = info.fdate;
    tm.tm_mday = fdate & 0x1f;
    fdate >>= 5;
    tm.tm_mon = (fdate & 0xf) - 1;
    fdate >>=4;
    tm.tm_year = fdate + 80;
    uint16_t ftime = info.ftime;
    tm.tm_sec = (ftime & 0x1f) * 2;
    ftime >>= 5;
    tm.tm_min = (ftime & 0x3f);
    ftime >>= 6;
    tm.tm_hour = (ftime & 0x1f);
    st->st_mtime = mktime(&tm);
    return 0;
}

static int vfs_fat_unlink(void* ctx, const char *path)
{
    FRESULT res = f_unlink(path);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    return 0;
}

static int vfs_fat_link(void* ctx, const char* n1, const char* n2)
{
    const size_t copy_buf_size = 4096;
    void* buf = malloc(copy_buf_size);
    if (buf == NULL) {
        errno = ENOMEM;
        return -1;
    }
    FIL f1;
    FRESULT res = f_open(&f1, n1, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) {
        goto fail1;
    }
    FIL f2;
    res = f_open(&f2, n2, FA_WRITE | FA_CREATE_NEW);
    if (res != FR_OK) {
        goto fail2;
    }
    size_t size_left = f_size(&f1);
    while (size_left > 0) {
        size_t will_copy = (size_left < copy_buf_size) ? size_left : copy_buf_size;
        size_t read;
        res = f_read(&f1, buf, will_copy, &read);
        if (res != FR_OK) {
            goto fail3;
        } else if (read != will_copy) {
            res = FR_DISK_ERR;
            goto fail3;
        }
        size_t written;
        res = f_write(&f2, buf, will_copy, &written);
        if (res != FR_OK) {
            goto fail3;
        } else if (written != will_copy) {
            res = FR_DISK_ERR;
            goto fail3;
        }
        size_left -= will_copy;
    }

fail3:
    f_close(&f2);
fail2:
    f_close(&f1);
fail1:
    free(buf);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    return 0;
}

static int vfs_fat_rename(void* ctx, const char *src, const char *dst)
{
    FRESULT res = f_rename(src, dst);
    if (res != FR_OK) {
        ESP_LOGD(TAG, "%s: fresult=%d", __func__, res);
        errno = fresult_to_errno(res);
        return -1;
    }
    return 0;
}
