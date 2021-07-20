/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2021 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#include "FileSystem.hxx"
#include "hardware.hxx"
// extern "C" required due to https://github.com/espressif/esp-idf/issues/7204
extern "C"
{
#include <dirent.h>
}
#include <driver/sdmmc_defs.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_types.h>
#include <driver/sdspi_host.h>
#include <esp_spiffs.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/logging.h>

namespace esp32cs
{

/// Partition name for the persistent filesystem.
static constexpr char FS_PARTITION[] = "fs";

/// Mount point for the persistent filesystem.
static constexpr char FS_MOUNTPOINT[] = "/fs";

static constexpr uint64_t ONE_MB = 1048576ULL;

static sdmmc_card_t *sd_card = nullptr;

void recursive_dump_tree(const std::string &path, bool remove = false, bool first = true)
{
    if (first && !remove)
    {
        LOG(INFO, "[FS] Dumping content of filesystem: %s", path.c_str());
    }
    DIR *dir = opendir(path.c_str());
    if (dir)
    {
        dirent *ent = NULL;
        while ((ent = readdir(dir)) != NULL)
        {
            string fullPath = path + "/" + ent->d_name;
            if (ent->d_type == DT_REG)
            {
                struct stat statbuf;
                stat(fullPath.c_str(), &statbuf);
                if (remove)
                {
                    LOG(VERBOSE, "[FS] Deleting %s (%lu bytes)", fullPath.c_str(), statbuf.st_size);
                    ERRNOCHECK(fullPath.c_str(), unlink(fullPath.c_str()));
                }
                else
                {
                    // NOTE: not using LOG(INFO, ...) here due to ctime injecting a
                    // newline at the end of the string.
                    printf("[FS] %s (%lu bytes) mtime: %s", fullPath.c_str(), statbuf.st_size, ctime(&statbuf.st_mtime));
                }
            }
            else if (ent->d_type == DT_DIR)
            {
                recursive_dump_tree(fullPath, remove, false);
            }
        }
        closedir(dir);
        if (remove)
        {
            rmdir(path.c_str());
        }
    }
    else
    {
        LOG_ERROR("[FS] Failed to open directory: %s", path.c_str());
    }
}

static sdmmc_host_t sd_host = SDSPI_HOST_DEFAULT();
static esp_vfs_fat_mount_config_t sd_cfg =
{
    .format_if_mount_failed = true,
    .max_files = 10,
    .allocation_unit_size = 0
};
static spi_bus_config_t bus_cfg =
{
    .mosi_io_num = CONFIG_SD_MOSI,
    .miso_io_num = CONFIG_SD_MISO,
    .sclk_io_num = CONFIG_SD_CLOCK,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .max_transfer_sz = 0,
    .flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO |
             SPICOMMON_BUSFLAG_MOSI,
    .intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM
};
static sdspi_device_config_t device_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();

bool mount_fs(bool cleanup)
{
    bool is_sd = false;
    esp_err_t err = ESP_FAIL;
    gpio_set_pull_mode((gpio_num_t)CONFIG_SD_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)CONFIG_SD_SELECT, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)CONFIG_SD_CLOCK, GPIO_PULLUP_ONLY);

    err = spi_bus_initialize(SDSPI_DEFAULT_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK)
    {
        err = esp_vfs_fat_sdspi_mount(FS_MOUNTPOINT, &sd_host, &device_cfg, &sd_cfg, &sd_card);
    }
    if (err == ESP_OK)
    {
        float capacity = ((uint64_t)sd_card->csd.capacity) * sd_card->csd.sector_size;
        LOG(INFO, "[FS] SD card '%s' mounted, max capacity %.2f MB", sd_card->cid.name, (float)(capacity / ONE_MB));
        FATFS *fs;
        DWORD c;
        if (f_getfree("0:", &c, &fs) == FR_OK)
        {
            float used_space = ((uint64_t)fs->csize * (fs->n_fatent - 2 - fs->free_clst)) * fs->ssize;
            float max_space = ((uint64_t)fs->csize * (fs->n_fatent - 2)) * fs->ssize;
            LOG(INFO, "[FS] SD FAT usage: %.2f/%.2f MB", (float)(used_space / ONE_MB), (float)(max_space / ONE_MB));
        }
        LOG(INFO, "[FS] SD will be used for persistent storage.");
        is_sd = true;
    }
    else
    {
        gpio_reset_pin((gpio_num_t)CONFIG_SD_MISO);
        gpio_reset_pin((gpio_num_t)CONFIG_SD_MOSI);
        gpio_reset_pin((gpio_num_t)CONFIG_SD_SELECT);
        gpio_reset_pin((gpio_num_t)CONFIG_SD_CLOCK);
        LOG(INFO, "[FS] SD Card not present or mounting failed, using SPIFFS");
        esp_vfs_spiffs_conf_t conf =
        {
            .base_path = FS_MOUNTPOINT,
            .partition_label = NULL,
            .max_files = 10,
            .format_if_mount_failed = true
        };
        // Attempt to mount the partition
        ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
        // check that the partition mounted
        size_t total = 0, used = 0;
        if (esp_spiffs_info(NULL, &total, &used) == ESP_OK)
        {
            LOG(INFO, "[FS] SPIFFS usage: %.2f/%.2f KiB", (float)(used / 1024.0f), (float)(total / 1024.0f));
        }
        else
        {
            LOG_ERROR("[FS] Unable to retrieve SPIFFS utilization statistics.");
        }
        LOG(INFO, "[FS] SPIFFS will be used for persistent storage.");
    }
    recursive_dump_tree(FS_MOUNTPOINT, cleanup);
    return is_sd;
}

void unmount_fs()
{
    // Unmount the SPIFFS partition
    if (esp_spiffs_mounted(NULL))
    {
        LOG(INFO, "[FS] Unmounting SPIFFS...");
        ESP_ERROR_CHECK(esp_vfs_spiffs_unregister(NULL));
    }

    // Unmount the SD card if it was mounted
    if (sd_card)
    {
        LOG(INFO, "[FS] Unmounting SD...");
        ESP_ERROR_CHECK(esp_vfs_fat_sdmmc_unmount());
        ESP_ERROR_CHECK(spi_bus_free(HSPI_HOST));
    }
}

} // namespace esp32cs