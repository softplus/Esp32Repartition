/**
 * @file part_mgr.cpp
 * @copyright Copyright (c) 2024 John Mueller
 * @brief This does the bulk of the partion management aka resizing. Very hacky.
 */

#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_flash_encrypt.h"
#include "esp_image_format.h"
#include "main.h"
#include "part_mgr.h"
#include "utils.h"
#include "device_info.h"
#include <MD5Builder.h>

// no-idea-dog.jpg

// Definitions from the ESP SDK & from poking around.
#define PARTITION_TABLE_SIZE 0x0C00
#define MAX_NUMBER_OF_PARTITIONS 10 // arbitrary, we just want to be sure we have things ok

typedef struct {
    uint16_t magic_id;                  /*!< It's magic */
    uint8_t type;                       /*!< partition type (app/data) */
    uint8_t subtype;                    /*!< partition subtype */
    uint32_t address;                   /*!< starting address of the partition in flash */
    uint32_t size;                      /*!< size of the partition, in bytes */
    char label[17];                     /*!< partition label, zero-terminated ASCII string */
    bool encrypted;                     /*!< flag is set to true if partition is encrypted */
    bool readonly;                      /*!< flag is set to true if partition is read-only */
} _my_esp_partition_t; // <- structure in partition table, from ESP SDK


typedef struct {
    uint32_t address_old;
    uint32_t address_new;
    uint32_t size_old;
    uint32_t size_new;
    bool action_erase;
    bool action_move;
} _my_partition_planner_t;

// get the address of the partition table; either 0x8000 or 0x9000; 0=failed
size_t getPartitionTableAddr() {
    // we're looking for "AA 50"
    static size_t cached_addr = 0;
    if (cached_addr != 0) {
        return cached_addr;
    }
    uint8_t b_buffer[4];
    for (size_t addr = 0x8000; addr < 0xA000; addr += 0x1000) {
        DEBUG_PRINTF("Checking for partition table at 0x%08x\n", addr);
        esp_err_t err = spi_flash_read(addr, b_buffer, sizeof(b_buffer));
        if (err != ESP_OK) {
            continue;
        }
        if (b_buffer[0] == 0xAA && b_buffer[1] == 0x50) {
            cached_addr = addr;
            return addr;
        }
    }
    DEBUG_PRINT("ERROR: Partition table not found.\n");
    return 0; // we got nothing, not even an error
}

// simple debugging & output display
void _add_output(std::unique_ptr<WebServer> & ws, const char *str) {
    ws->sendContent(str, strlen(str));
    DEBUG_PRINT(str);
};
void _add_output(std::unique_ptr<WebServer> & ws, const String& str) {
    _add_output(ws, str.c_str());
};

// Expand app partitions to our ideal size, output to response 
void partition_mgr_fix(std::unique_ptr<WebServer> & ws, bool test_only) {
    char c_buffer[256];

    // 1. confirm first app partition is active
    // 2. Check if partition table findable
    // 3. copy partition table to local buffer
    // 4. confirm that order is app, app, data; else fail
    // 5. calculate new data partition size
    // 6. erase data partition, app1 partition
    // 7. write partition table
    // 8. reboot.

    // show device info
    getDeviceInfo(c_buffer, sizeof(c_buffer)); // get hardware info
    _add_output(ws, c_buffer);
    _add_output(ws, "\n");
    snprintf(c_buffer, sizeof(c_buffer), "Partition table address: 0x%x\n", getPartitionTableAddr());
    _add_output(ws, c_buffer);
    getBootloaderMd5(c_buffer, sizeof(c_buffer));
    _add_output(ws, F("Bootloader MD5: "));
    _add_output(ws, c_buffer);
    _add_output(ws, "\n\n");

    // info text
    if (!test_only) {
        _add_output(ws, "NOTE: If you do not see a line with 'Ready' at the end,\nthis process didn't work.\n\n");
    }

    // Check for flash encryption - we can't do anything if it's encrypted
    // Rewriting the partition table will brick your device.
    if (esp_flash_encryption_enabled()) {
        _add_output(ws, "ERROR: Flash encryption is enabled. Can't continue.\n");
        return;
    }

    // 1. confirm first app partition is active
    const esp_partition_t* p_running = esp_ota_get_running_partition();
    const esp_partition_t* p_next = esp_ota_get_next_update_partition(NULL);
    if (p_next == NULL) {
        _add_output(ws, "ERROR: There is only one app partition.\n");
        return;
    }
    if (p_running->address > p_next->address) {
        _add_output(ws, "ERROR: YOU MUST UPLOAD THE FIRMWARE AGAIN.\n");
        _add_output(ws, "The current partition is not the first one.\n");
        _add_output(ws, "<a href='/update'>Upload firmware again</a>\n");
        return;
    }
    _add_output(ws, "Current app parition is first: OK\n");

    // 2. Check if partition table findable
    if (getPartitionTableAddr() == 0) {
        _add_output(ws, "ERROR: Partition table not found. Can't continue.\n");
        return;
    }

    // 3. Copy partition table to local buffer
    _add_output(ws, "Reading partition table...\n");
    char *partition_buffer = (char *)malloc(SPI_FLASH_SEC_SIZE+1);
    if (partition_buffer == NULL) {
        _add_output(ws, "Failed to allocate memory for partition buffer\n");
        return;
    }
    _my_esp_partition_t *partitions[MAX_NUMBER_OF_PARTITIONS] = {NULL};
    esp_err_t err =
        spi_flash_read(getPartitionTableAddr(), partition_buffer, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        free(partition_buffer);
        return;
    }

    // separate out partitions
    _add_output(ws, "Splitting partitions out...\n");
    unsigned short partition_count = 0;
    size_t md5_offset = 0;

    for (size_t offset = 0; offset < SPI_FLASH_SEC_SIZE; offset += 32) {
        if ((*(partition_buffer+offset)==0xAA) && (*(partition_buffer+offset+1)==0x50)) {
            partitions[partition_count] = (_my_esp_partition_t*)(partition_buffer + offset);
            partition_count++;
            if (partition_count>MAX_NUMBER_OF_PARTITIONS) break;
        } else if ((*(partition_buffer+offset)==0xEB) && (*(partition_buffer+offset+1)==0xEB)) {
            md5_offset = offset;
        }
    }
    _add_output(ws, "Created local copy of partiton table: OK\n");
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "Type: %02x / %02x, Addr: 0x%06x, Size: 0x%06x (%dK): %s\n",
                partitions[i]->type, partitions[i]->subtype, partitions[i]->address, partitions[i]->size, (int)(partitions[i]->size/1024), partitions[i]->label);
        _add_output(ws, c_buffer);
    }
    _add_output(ws, "\n");

    // 4. Confirm we have min 2x app, and min 1 data
    int app_count = 0, data_count = 0;
    for (int i=0; i<partition_count; i++) {
        if (partitions[i]->type == ESP_PARTITION_TYPE_APP) {
            app_count++;
        } else if (partitions[i]->type == ESP_PARTITION_TYPE_DATA) {
            data_count++;
        }
    }
    if ((app_count < 2) || (data_count < 1)) {
        _add_output(ws, "ERROR: Need 2+ app, 1+ data partitions; can't continue.\n");
        free(partition_buffer);
        return;
    }

    // make planning copy
    _my_partition_planner_t planner[MAX_NUMBER_OF_PARTITIONS];
    for (int i=0; i<partition_count; i++) {
        planner[i].address_old = partitions[i]->address;
        planner[i].size_old = partitions[i]->size;
        // defaults
        planner[i].address_new = 0; planner[i].size_new = 0;
        planner[i].action_erase = false; planner[i].action_move = false;
    }

    // plan to resize app partitions
    uint32_t size_delta = 0;
    int first_app_index = -1;
    for (int i=0; i<partition_count; i++) {
        if (partitions[i]->type == ESP_PARTITION_TYPE_APP && 
            (partitions[i]->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
             partitions[i]->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
            if (partitions[i]->size < RESIZE_APP_PARTITION_SIZE) {
                size_delta += RESIZE_APP_PARTITION_SIZE - partitions[i]->size;
                planner[i].size_new = RESIZE_APP_PARTITION_SIZE;
                if (first_app_index == -1) {
                    first_app_index = i;
                } else {
                    planner[i].action_erase = true;
                }
            }
        }
    }
    if (size_delta == 0) {
        _add_output(ws, "UNNECESSARY: App partitions are already ideal size.\n");
        _add_output(ws, "READY TO GO - upload the firmware you want.\n");
        _add_output(ws, "<a href='/update'>Upload new firmware</a>\n");
        free(partition_buffer);
        return;
    }

    // find biggest data partition to shrink
    int biggest_data_index = 0; uint32_t biggest_data_size = 0;
    for (int i=0; i<partition_count; i++) {
        if (partitions[i]->type == ESP_PARTITION_TYPE_DATA) {
            if (partitions[i]->size > biggest_data_size) {
                biggest_data_size = partitions[i]->size;
                biggest_data_index = i;
            }
        }
    }
    if (biggest_data_size < size_delta) {
        _add_output(ws, "ERROR: Data partition is not large enough.\n");
        free(partition_buffer);
        return;
    }
    planner[biggest_data_index].size_new = biggest_data_size - size_delta;

    // iterate through all partitions
    uint32_t address_offset = 0;
    for (int i=0; i<partition_count; i++) {
        if (address_offset>0) {
            planner[i].address_new = planner[i].address_old + address_offset;
            if (!planner[i].action_erase) {
                planner[i].action_move = true;
                if (!planner[i].size_new) planner[i].size_new = planner[i].size_old;
            }
        }
        if (planner[i].size_new != 0) {
            address_offset += planner[i].size_new - planner[i].size_old;
        }
    }
    _add_output(ws, "Partition table has 2+x app, 1+x data: OK\n");

    // 5. update partition table based on new addresses + sizes
    for (int i=0; i<partition_count; i++) {
        if (planner[i].address_new != 0) {
            partitions[i]->address = planner[i].address_new;
        }
        if (planner[i].size_new != 0) {
            partitions[i]->size = planner[i].size_new;
        }
    }

    // show new partition table
    _add_output(ws, "New partition table:\n");
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "Type: %02x / %02x, Addr: 0x%06x, Size: 0x%06x (%dK): %s\n",
                partitions[i]->type, partitions[i]->subtype, partitions[i]->address, partitions[i]->size, (int)(partitions[i]->size/1024), partitions[i]->label);
        _add_output(ws, c_buffer);
    }

    if (test_only) {
        _add_output(ws, "\nEverything looks good! Try it for real now!\n");
        free(partition_buffer);
        return;
    }
    _add_output(ws, "\nDoing the work now...\n");

    // calculate md5 of new partition table
    MD5Builder _md5 = MD5Builder();
    _md5.begin();
    _md5.add((uint8_t*)partition_buffer, md5_offset);
    _md5.calculate();
    _md5.getBytes((uint8_t*)(partition_buffer + md5_offset + 16));
   
    // write partition table buffer back to flash
    unsigned long time_start = micros();
    _add_output(ws, "Erasing partition table...\n");
    err = spi_flash_erase_range(getPartitionTableAddr(), SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        free(partition_buffer);
        return;
    }
    _add_output(ws, "Writing partition table...\n");
    err = spi_flash_write(getPartitionTableAddr(), partition_buffer, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to write partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        free(partition_buffer);
        return;
    }
    unsigned long time_end = micros();
    snprintf(c_buffer, sizeof(c_buffer), " ... Partition table written in %lu ms\n", (time_end-time_start)/1000);
    _add_output(ws, c_buffer);

    uint8_t* move_buffer = (uint8_t*)malloc(SPI_FLASH_SEC_SIZE);
    if (move_buffer == NULL) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to allocate memory for buffer\n");
        _add_output(ws, c_buffer);
        free(partition_buffer);
        return;
    }

    // time to clean up partitions
    for (int i = partition_count - 1; i >= 0; i--) {
        if (planner[i].action_erase) {
            snprintf(c_buffer, sizeof(c_buffer), "Erasing partition %i at 0x%x, length 0x%x\n", 
                    i, planner[i].address_new, planner[i].size_new);
            _add_output(ws, c_buffer);
            time_start = micros();
            err = spi_flash_erase_range(planner[i].address_old, planner[i].size_old);
            if (err != ESP_OK) {
                snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition: 0x%x\n", err);
                _add_output(ws, c_buffer);
            } else {
                time_end = micros();
                snprintf(c_buffer, sizeof(c_buffer), " ... Partition erased in %lu ms\n", (time_end-time_start)/1000);
                _add_output(ws, c_buffer);
            }
        }

        if (planner[i].action_move && (planner[i].address_new!=planner[i].address_old)) {
            snprintf(c_buffer, sizeof(c_buffer), "Moving partition %i from 0x%x to 0x%x length 0x%x ...\n ",
                     i, planner[i].address_old, planner[i].address_new, planner[i].size_new);
            _add_output(ws, c_buffer);
            int counter = 0;
            time_start = micros();
            for (int32_t j = planner[i].size_new - SPI_FLASH_SEC_SIZE; j >= 0; j -= SPI_FLASH_SEC_SIZE) {
                snprintf(c_buffer, sizeof(c_buffer), "0x%x  ", planner[i].address_old + j);
                _add_output(ws, c_buffer);
                counter++; if (counter % 8 == 0) _add_output(ws, "\n ");
                err = spi_flash_read(planner[i].address_old + j, move_buffer, SPI_FLASH_SEC_SIZE);
                if (err != ESP_OK) {
                    snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition chunk: 0x%x\n", err);
                    _add_output(ws, c_buffer);
                    free(move_buffer);
                    free(partition_buffer);
                    break;
                }
                err = spi_flash_erase_range(planner[i].address_new+j, SPI_FLASH_SEC_SIZE);
                if (err != ESP_OK) {
                    snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition: 0x%x\n", err);
                    _add_output(ws, c_buffer);
                    // whatever, we will continue
                }
                err = spi_flash_write(planner[i].address_new + j, move_buffer, SPI_FLASH_SEC_SIZE);
                if (err != ESP_OK) {
                    snprintf(c_buffer, sizeof(c_buffer), "Failed to write partition chunk: 0x%x\n", err);
                    _add_output(ws, c_buffer);
                    // we will also continue
                }
            }
            if (counter % 8 != 0) _add_output(ws, "\n ");
            time_end = micros();
            snprintf(c_buffer, sizeof(c_buffer), " ... Partition moved %i sectors in %lu ms (%lu ms/sector)\n", 
                counter, (time_end-time_start)/1000, (time_end-time_start)/(1000*counter));
            _add_output(ws, c_buffer);
        }
    }
    free(move_buffer);

    _add_output(ws, "Partitions erased / moved: OK\n");

    _add_output(ws, "Partition table updated.\n\n");
    _add_output(ws, "READY! After reboot, upload the firmware that you need.\n\n");

    _add_output(ws, "Rebooting...\n");
    _add_output(ws, "\n");
    _add_output(ws, HTML_OUTRO); // unless we already rebooted, lol

    time_start = millis();
    while (millis() - time_start < 2000) delay(100); // non-blocking delay

    _add_output(ws, "\n"); // sometimes it just doesn't send the rest. this is a hack.
    free(partition_buffer);

    // send results
    ESP.restart();
}
