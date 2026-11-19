/*
 * Copyright (c) 2019 Tavish Naruka <tavishnaruka@gmail.com>
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2023 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample which uses the filesystem API and SDHC driver */

#include "vitrolinha/track.h"
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include <ff.h>

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"

#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};

LOG_MODULE_REGISTER(storage);

#define MAX_PATH 128
/* #define SOME_FILE_NAME "escala.cs" */
/* #define SOME_DIR_NAME "escala.csv" */
/* #define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))
 */

int lsdir(const char *path);

static const char *disk_mount_pt = DISK_MOUNT_PT;

int storage_init(struct track_meta *out) {
    mp.mnt_point = disk_mount_pt;

    int res = fs_mount(&mp);

    if (res == FR_OK) {
        printk("Disk mounted.\n");
        /* Try to unmount and remount the disk */
        res = fs_unmount(&mp);
        if (res != FR_OK) {
            printk("Error unmounting disk\n");
            return res;
        }
        res = fs_mount(&mp);
        if (res != FR_OK) {
            printk("Error remounting disk\n");
            return res;
        }
    }
    return 0;
}

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
int lsdir(const char *path) {
    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    int count = 0;

    fs_dir_t_init(&dirp);

    /* Verify fs_opendir() */
    res = fs_opendir(&dirp, path);
    if (res) {
        LOG_INF("Error opening dir %s [%d]\n", path, res);
        return res;
    }

    LOG_INF("\nListing dir %s ...\n", path);
    for (;;) {
        /* Verify fs_readdir() */
        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0) {
            break;
        }

        if (entry.type == FS_DIR_ENTRY_DIR) {
            LOG_INF("[DIR ] %s\n", entry.name);
        } else {
            LOG_INF("[FILE] %s (size = %zu)\n", entry.name, entry.size);
        }
        count++;
    }

    /* Verify fs_closedir() */
    fs_closedir(&dirp);
    if (res == 0) {
        res = count;
    }

    return res;
}
