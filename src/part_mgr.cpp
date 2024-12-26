/**
 * @file part_mgr.cpp
 * @copyright Copyright (c) 2024 John Mueller
 * @brief This does the bulk of the partion management aka resizing. Very hacky.
 */

#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "main.h"
#include "part_mgr.h"
#include "utils.h"
#include "device_info.h"
#include <MD5Builder.h>

// no-idea-dog.jpg

// Definitions from the ESP SDK & from poking around.
#define PARTITION_TABLE_SIZE 0x0C00
#define RESIZE_APP_PARTITION_SIZE 0x180000 // (1536K)
#define MAX_NUMBER_OF_PARTITIONS 10 // arbitrary, we just want to be sure we have things ok

typedef struct {
    uint16_t flash_chip;                /*!< SPI flash chip on which the partition resides */
    uint8_t type;                       /*!< partition type (app/data) */
    uint8_t subtype;                    /*!< partition subtype */
    uint32_t address;                   /*!< starting address of the partition in flash */
    uint32_t size;                      /*!< size of the partition, in bytes */
    //uint32_t erase_size;              /*!< size the erase operation should be aligned to */
    char label[17];                     /*!< partition label, zero-terminated ASCII string */
    bool encrypted;                     /*!< flag is set to true if partition is encrypted */
    bool readonly;                      /*!< flag is set to true if partition is read-only */
} _my_esp_partition_t; // <- structure in partition table, from ESP SDK

// get the address of the partition table; either 0x8000 or 0x9000; 0=failed
size_t getPartitionTableAddr() {
    // we're looking for "AA 50"
    static size_t cached_addr = 0;
    if (cached_addr != 0) {
        return cached_addr;
    }
    uint8_t b_buffer[256];
    for (size_t addr = 0; addr < 0xA000; addr += 0x1000) {
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
    //ws->sendContent(str.c_str(), str.length());
    //DEBUG_PRINT(str);
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
    _add_output(ws, "NOTE: If you do not see a line with 'Ready' at the end, this process didn't work.\n\n");

    // 1. confirm first app partition is active
    const esp_partition_t* p_running = esp_ota_get_running_partition();
    const esp_partition_t* p_next = esp_ota_get_next_update_partition(NULL);
    if (p_running->address > p_next->address) {
        _add_output(ws, "ERROR: YOU MUST UPLOAD THE FIRMWARE AGAIN.\n");
        _add_output(ws, "The current partition is not the first one.\n");
        _add_output(ws, "<a href='/update'>Upload firmware again</a>\n");
        return;
    }
    _add_output(ws, "Current app parition is first: OK!\n");

    // 2. Check if partition table findable
    if (getPartitionTableAddr() == 0) {
        _add_output(ws, "ERROR: Partition table not found. Can't continue.\n");
        return;
    }

    // 3. Copy partition table to local buffer
    _add_output(ws, "Reading partition table...\n");
    char *partition_buffer = (char *)malloc(SPI_FLASH_SEC_SIZE+1);
    _my_esp_partition_t *partitions[MAX_NUMBER_OF_PARTITIONS];
    esp_err_t err =
        spi_flash_read(getPartitionTableAddr(), partition_buffer, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Done reading.\n");

    // separate out partitions
    _add_output(ws, "Splitting partitions out...\n");
    unsigned short partition_count = 0;
    size_t md5_offset = 0;

    bool any_errors = false;
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
        snprintf(c_buffer, sizeof(c_buffer), "Type: %02x, Subtype: %02x, Addr: 0x%08x, Size: 0x%08x (%dK), Label: %s\n",
                partitions[i]->type, partitions[i]->subtype, partitions[i]->address, partitions[i]->size, (int)(partitions[i]->size/1024), partitions[i]->label);
        _add_output(ws, c_buffer);
    }
    _add_output(ws, "\n");

    // 4. confirm that order is app, app, data; else fail
    bool is_good = false;
    int app0_index = -1, app1_index = -1, data_index = -1;
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "... Checking index %d, Label: %s\n",
                i, partitions[i]->label);
        _add_output(ws, c_buffer);  
        if (partitions[i]->type == ESP_PARTITION_TYPE_APP) { // first app partition
            app0_index = i;
            if (i+2>partition_count) {
                _add_output(ws, "ERROR: not enough paritions after first app partition.\n");
                any_errors = true;
                break;
            }
            if (partitions[i+1]->type != ESP_PARTITION_TYPE_APP) {
                _add_output(ws, "ERROR: no sequential app partitions.\n");
                any_errors = true;
                break;
            }
            app1_index = i+1;
            if (partitions[i+2]->type != ESP_PARTITION_TYPE_DATA) {
                _add_output(ws, "ERROR: no data partition after app partitions.\n");
                any_errors = true;
                break;
            }
            data_index = i+2;
            if (i+2==partition_count-1) {
                is_good = true;
                break;
            }
            snprintf(c_buffer, sizeof(c_buffer), "ERROR: extra partitions after data partition; index=%d, count=%d\n",
                i, partition_count);
            _add_output(ws, c_buffer);
            any_errors = true;
            break;
        }
    }
    if (!is_good) {
        _add_output(ws, "ERROR: partition table does not have app, app, data order.\n");
        any_errors = true;
    }
    if (any_errors) {
        _add_output(ws, "Aborting.");
        return;
    }
    _add_output(ws, "Partition order is app, app, data: OK\n");

    // check if app actually needs resizing
    if (partitions[app1_index]->size >= RESIZE_APP_PARTITION_SIZE && 
        partitions[app0_index]->size >= RESIZE_APP_PARTITION_SIZE) {
        _add_output(ws, "\nUNNECESSARY: App partitions are already ideal size.\n");
        _add_output(ws, "\nREADY TO GO - upload the firmware you want.\n");
        _add_output(ws, "<a href='/update'>Upload new firmware</a>\n");
        return;
    }
    // check if data partition is large enough
    if (partitions[data_index]->size >= ( 
        (partitions[app1_index]->size - RESIZE_APP_PARTITION_SIZE) +
        (partitions[app0_index]->size - RESIZE_APP_PARTITION_SIZE) ) ) {
        _add_output(ws, "ERROR: Data partition is not large enough to accomodate new app partitions.\n");
        return;
    }
    _add_output(ws, "Data partition is large enough to accomodate new app partitions: OK\n");
    _add_output(ws, "\n");

    // 5. calculate new data partition size
    uint32_t new_data_size = (partitions[data_index]->size - 
        ((RESIZE_APP_PARTITION_SIZE - partitions[app1_index]->size) +
         (RESIZE_APP_PARTITION_SIZE - partitions[app0_index]->size)) );
    uint32_t new_data_address = partitions[data_index]->address + 
        (RESIZE_APP_PARTITION_SIZE - partitions[app1_index]->size) +
        (RESIZE_APP_PARTITION_SIZE - partitions[app0_index]->size);
    uint32_t new_app1_address = partitions[app1_index]->address + 
        (RESIZE_APP_PARTITION_SIZE - partitions[app0_index]->size);
    snprintf(c_buffer, sizeof(c_buffer), "Old: app0 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app0_index]->address, partitions[app0_index]->size, (int)(partitions[app0_index]->size/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "Old: app1 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app1_index]->address, partitions[app1_index]->size, (int)(partitions[app1_index]->size/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "Old: data partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[data_index]->address, partitions[data_index]->size, (int)(partitions[data_index]->size/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: app0 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app0_index]->address, RESIZE_APP_PARTITION_SIZE, (int)(RESIZE_APP_PARTITION_SIZE/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: app1 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_app1_address, RESIZE_APP_PARTITION_SIZE, (int)(RESIZE_APP_PARTITION_SIZE/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: data partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_data_address, new_data_size, (int)(new_data_size/1024));
    _add_output(ws, c_buffer);
    if (new_data_address - partitions[data_index]->address < SPI_FLASH_SEC_SIZE) {
        _add_output(ws, "ERROR: New data partition address offset by at least a sector.\n");
        return;
    }

    if (test_only) {
        _add_output(ws, "\nEverything looks good! Try it for real now!\n");
        return;
    }
    _add_output(ws, "\nDoing the work now...\n");

    // 6. erase data partition, app1 partition
    // we're just erasing the first sector, should be ok
    _add_output(ws, "Erasing data partition...\n");
    err = spi_flash_erase_range(new_data_address, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase data partition: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }

    _add_output(ws, "Erasing app1 partition...\n");
    err = spi_flash_erase_range(new_app1_address, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase app1 partition: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Partitions data + app1 erased: OK\n");
    
    // 7. write partition table
    _add_output(ws, "Updating partition table...\n");
    partitions[data_index]->address = new_data_address;
    partitions[data_index]->size = new_data_size;
    partitions[app1_index]->address = new_app1_address;
    partitions[app1_index]->size = RESIZE_APP_PARTITION_SIZE;
    partitions[app0_index]->size = RESIZE_APP_PARTITION_SIZE;

    // calculate md5 of partition table
    MD5Builder _md5 = MD5Builder();
    _md5.begin();
    _md5.add((uint8_t*)partition_buffer, md5_offset);
    _md5.calculate();
    _md5.getBytes((uint8_t*)(partition_buffer + md5_offset + 16));
   
    // mostly just to double-check. You need to set these when compiling ESP-IDF with menuconfig.
#ifdef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS is enabled. Won't work.\n");
#endif
#ifndef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED is not enabled. Won't work.\n");
#endif
#ifdef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED is enabled. Should work.\n");
#endif

    // write partition table buffer back to flash
    _add_output(ws, "Erasing partition table...\n");
    err = spi_flash_erase_range(getPartitionTableAddr(), SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Writing partition table...\n");
    err = spi_flash_write(getPartitionTableAddr(), partition_buffer, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to write partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Partition table rewritten: OK\n");

    
    _add_output(ws, "Partition table updated.\n\n");
    _add_output(ws, "READY! After reboot, upload the firmware that you need.\n\n");

    _add_output(ws, "Rebooting...\n");
    _add_output(ws, "\n");
    _add_output(ws, HTML_OUTRO); // unless we already rebooted, lol

    delay(2000); // to send result before reboot
    _add_output(ws, "\n"); // sometimes it just doesn't send the rest. this is a hack.

    // send results
    ESP.restart();
    return;
}
