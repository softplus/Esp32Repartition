#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "part_mgr.h"

// wild ass crazy shit
#define PARTITION_TABLE_SIZE 0x0C00 // 0x1000
#define PARTITION_TABLE_ADDRESS 0x8000
typedef struct {
    uint16_t flash_chip;                /*!< SPI flash chip on which the partition resides */
uint8_t type;                           /*!< partition type (app/data) */
uint8_t subtype;                        /*!< partition subtype */
    uint32_t address;                   /*!< starting address of the partition in flash */
    uint32_t size;                      /*!< size of the partition, in bytes */
    //uint32_t erase_size;              /*!< size the erase operation should be aligned to */
    char label[17];                     /*!< partition label, zero-terminated ASCII string */
    bool encrypted;                     /*!< flag is set to true if partition is encrypted */
    bool readonly;                      /*!< flag is set to true if partition is read-only */
} _my_esp_partition_t; // <- structure in partition table, from esp SDK

// dump a memory block to a char[] as hex, for debugging
void hex_dump(char *output, int max_len, const uint8_t *data, int data_len) {
    static const char hex_chars[] = "0123456789abcdef";
    int lines = data_len / 16;
    int needed_len = lines * 65 + 1;
    
    if (max_len < needed_len) {
        output[0] = '\0';
        return;
    }
    
    char *ptr = output;
    for (int i = 0; i < data_len; i++) {
        // Hex part
        *ptr++ = hex_chars[data[i] >> 4];
        *ptr++ = hex_chars[data[i] & 0x0F];
        *ptr++ = ' ';
        
        // At end of line, add ASCII
        if ((i + 1) % 16 == 0) {
            const uint8_t *line = data + i - 15;
            for (int j = 0; j < 16; j++) {
                *ptr++ = (line[j] >= 32 && line[j] <= 126) ? line[j] : '.';
            }
            *ptr++ = '\n';
        }
    }
    *ptr = '\0';
}
char output_buffer[4096]; 
auto _add_output = [&] (std::unique_ptr<WebServer> & ws, const char *str) {
//auto _add_output = [&] (WebServer ws, const char *str) {
    ws->sendContent(str, strlen(str));
    DEBUG_PRINT(str); // with #define DEBUG_PM enabled in part_mgr.h, this will print to Serial
};

// Read partitions, output to response 
//void partition_mgr_read(WebServer ws) {
void partition_mgr_read(std::unique_ptr<WebServer> & ws) {

    // must initialize webserver output as chunked beforehand

    uint8_t b_buffer[256];
    char c_buffer[256];
    output_buffer[0] = 0; // reset output
    _add_output(ws, "Partition:\n");

    for (int offset = 0; offset < PARTITION_TABLE_SIZE; offset += 256) {
      esp_err_t err =
          spi_flash_read(PARTITION_TABLE_ADDRESS + offset, b_buffer, 256);
      if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Addr: 0x%08x\n", PARTITION_TABLE_ADDRESS + offset);
        _add_output(ws, c_buffer);
        snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        continue;
      }
      if (offset == 0 && (b_buffer[0] != 0xAA || b_buffer[1] != 0x50)) {
        snprintf(c_buffer, sizeof(c_buffer), "Addr: 0x%08x\n", PARTITION_TABLE_ADDRESS + offset);
        _add_output(ws, c_buffer);
        snprintf(c_buffer, sizeof(c_buffer), "Invalid existing partition table. Maybe bad offset.\n");
        _add_output(ws, c_buffer);
        continue;
      }
      if (b_buffer[0]!=0xAA || b_buffer[1]!=0x50) {
        continue;
      }
      snprintf(c_buffer, sizeof(c_buffer), "Addr: 0x%08x\n", PARTITION_TABLE_ADDRESS + offset);
      _add_output(ws, c_buffer);
      for (int i = 0; i < 256; i+=32) {
        if ((b_buffer[i]==0xAA) && (b_buffer[i+1]==0x50)) {
          _my_esp_partition_t *party = (_my_esp_partition_t*)(b_buffer + i);
          snprintf(c_buffer, sizeof(c_buffer), "Type: %02x, Subtype: %02x, Addr: 0x%08x, Size: 0x%08x (%dK), Label: %s\n",
                  party->type, party->subtype, party->address, party->size, (int)(party->size/1024), party->label);
          _add_output(ws, c_buffer);
          //hex_dump(c_buffer, sizeof(c_buffer), b_buffer+i, 32);
          //_add_output(c_buffer);
        }
      }
    }

    // inform of current vs next partition
    const esp_partition_t* p_running = esp_ota_get_running_partition();
    const esp_partition_t* p_next = esp_ota_get_next_update_partition(NULL);

    snprintf(c_buffer, sizeof(c_buffer), "Running: Addr: 0x%08x, Label: %s\n",
            p_running->address, p_running->label);
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "Next:    Addr: 0x%08x, Label: %s\n",
            p_next->address, p_next->label);
    _add_output(ws, c_buffer);

    if (p_running->address < p_next->address) {
      snprintf(c_buffer, sizeof(c_buffer), "Current partition is first; you are ready.\n");
    } else {
      snprintf(c_buffer, sizeof(c_buffer), "YOU MUST UPLOAD AN UPDATE FIRST. Current partition is the later one.\n");
    }
    _add_output(ws, c_buffer);
}


// Expand app partitions to our ideal size, output to response 
void partition_mgr_fix(std::unique_ptr<WebServer> & ws) {
    char c_buffer[256];
    output_buffer[0] = 0;
//#define IDEAL_APP_PARTITION_SIZE 0x00180000 // (1536K)
#define IDEAL_APP_PARTITION_SIZE 0x00180000 // (1536K)

    // 1. confirm first app partition is active
    // 2. copy partition table to local buffer
    // 3. confirm that order is app, app, data; else fail
    // 4. freeze processing
    // 5. calculate new data partition size
    // 6. erase data partition, app1 partition
    // 7. write partition table
    // 11. thoughts and prayers. restore processing. reboot.


    _add_output(ws, "If you do not see a line with 'Ready' at the end, this process didn't work.\n\n");

    // 1. confirm first app partition is active
    const esp_partition_t* p_running = esp_ota_get_running_partition();
    const esp_partition_t* p_next = esp_ota_get_next_update_partition(NULL);
    if (p_running->address > p_next->address) {
        snprintf(c_buffer, sizeof(c_buffer), "ERROR: YOU MUST UPLOAD AN UPDATE FIRST. Current partition is the later one.\n");
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Current app parition is first: OK!\n");

    // 2. copy partition table to local buffer
    _add_output(ws, "Reading partition table...\n");
    char *partition_buffer = (char *)malloc(SPI_FLASH_SEC_SIZE+1);
    _my_esp_partition_t *partitions[10];
    esp_err_t err =
        spi_flash_read(PARTITION_TABLE_ADDRESS, partition_buffer, 2048);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Done reading.\n");

    // separate out partitions
    _add_output(ws, "Splitting partitions out...\n");
    int partition_count = 0;
    bool any_errors = false;
    for (uint32_t offset = 0; offset < SPI_FLASH_SEC_SIZE; offset += 32) {
        if ((*(partition_buffer+offset)==0xAA) && (*(partition_buffer+offset+1)==0x50)) {
            partitions[partition_count] = (_my_esp_partition_t*)(partition_buffer + offset);
            partition_count++;
            if (partition_count>10) break;
        }
    }
    _add_output(ws, "Created local copy of partiton table: OK\n");
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "Type: %02x, Subtype: %02x, Addr: 0x%08x, Size: 0x%08x (%dK), Label: %s\n",
                partitions[i]->type, partitions[i]->subtype, partitions[i]->address, partitions[i]->size, (int)(partitions[i]->size/1024), partitions[i]->label);
        _add_output(ws, c_buffer);
    }

    // 3. confirm that order is app, app, data; else fail
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
    if (partitions[app1_index]->size >= IDEAL_APP_PARTITION_SIZE && 
        partitions[app0_index]->size >= IDEAL_APP_PARTITION_SIZE) {
        _add_output(ws, "UNNECESSARY: App partitions are already ideal size: OK\n");
        return;
    }
    // check if data partition is large enough
    if (partitions[data_index]->size >= ( 
        (partitions[app1_index]->size - IDEAL_APP_PARTITION_SIZE) +
        (partitions[app0_index]->size - IDEAL_APP_PARTITION_SIZE) ) ) {
        _add_output(ws, "ERROR: Data partition is not large enough to accomodate new app partitions.\n");
        return;
    }
    _add_output(ws, "Data partition is large enough to accomodate new app partitions: OK\n");

    // 4. freeze processing
    //WLED::instance().disableWatchdog();
    //usermods.onUpdateBegin(true); // notify usermods that update is about to begin (some may require task de-init)

    // 5. calculate new data partition size
    uint32_t new_data_size = (partitions[data_index]->size - 
        ((IDEAL_APP_PARTITION_SIZE - partitions[app1_index]->size) +
         (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size)) );
    uint32_t new_data_address = partitions[data_index]->address + 
        (IDEAL_APP_PARTITION_SIZE - partitions[app1_index]->size) +
        (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size);
    uint32_t new_app1_address = partitions[app1_index]->address + 
        (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size);
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
            partitions[app0_index]->address, IDEAL_APP_PARTITION_SIZE, (int)(IDEAL_APP_PARTITION_SIZE/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: app1 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_app1_address, IDEAL_APP_PARTITION_SIZE, (int)(IDEAL_APP_PARTITION_SIZE/1024));
    _add_output(ws, c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: data partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_data_address, new_data_size, (int)(new_data_size/1024));
    _add_output(ws, c_buffer);
    if (new_data_address - partitions[data_index]->address < SPI_FLASH_SEC_SIZE) {
        _add_output(ws, "ERROR: New data partition address offset by at least a sector.\n");
        return;
    }

    // 6. erase data partition, app1 partition
    // we're just erasing the first sector, because we're lazy
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
    partitions[app1_index]->size = IDEAL_APP_PARTITION_SIZE;
    partitions[app0_index]->size = IDEAL_APP_PARTITION_SIZE;

#ifdef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS is enabled. Won't work.\n");
#endif
#ifndef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED is not enabled. Won't work.\n");
#endif
#ifdef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
    _add_output(ws, "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED is enabled. Should work.\n");
#endif

    //_add_output(ws, "Checking for dangerous access...\n");
    //bool _isok = is_safe_write_address(PARTITION_TABLE_ADDRESS, 2048);

    // write partition table buffer back to flash
    _add_output(ws, "Erasing partition table...\n");
    err = spi_flash_erase_range(PARTITION_TABLE_ADDRESS, 256);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Writing partition table...\n");
    err = spi_flash_write(PARTITION_TABLE_ADDRESS, partition_buffer, 2048);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to write partition table: 0x%x\n", err);
        _add_output(ws, c_buffer);
        return;
    }
    _add_output(ws, "Partition table rewritten: OK\n");

    
    _add_output(ws, "Ready...\n");

    // 10. update partition table checksum // lol, how
    // 11. thoughts and prayers. restore processing. reboot.
    //WLED::instance().enableWatchdog();
    _add_output(ws, "Partition table updated. Rebooting...");
    _add_output(ws, "<a href='/'>Click here to go to home</a>");

    // send results
    ESP.restart();
    return;
}


/*

// Expand app partitions to our ideal size, output to response 
void partition_mgr_fix_2(AsyncWebServerRequest *request) {
    uint8_t b_buffer[256];
    char c_buffer[256];
    output_buffer[0] = 0;
//#define IDEAL_APP_PARTITION_SIZE 0x00180000 // (1536K)
#define IDEAL_APP_PARTITION_SIZE 0x00181000 // (1536K)

    // 1. confirm first app partition is active
    // 2. copy partition table to local buffer
    // 3. confirm that order is app, app, data; else fail
    // 4. freeze processing
    // 5. calculate new data partition size
    // 6. shift data partition in sector-chunks to new location
    // 7. update data partition address and size in partition table
    // 8. update app1 partition address and size in partition table
    // 9. update app0 partition size in partition table
    // 10. update partition table checksum // lol, how
    // 11. thoughts and prayers. restore processing. reboot.


    // 1. confirm first app partition is active
    const esp_partition_t* p_running = esp_ota_get_running_partition();
    const esp_partition_t* p_next = esp_ota_get_next_update_partition(NULL);
    if (p_running->address > p_next->address) {
        snprintf(c_buffer, sizeof(c_buffer), "ERROR: YOU MUST UPLOAD AN UPDATE FIRST. Current partition is the later one.\n");
        _add_output(c_buffer);
        request->send(500, "text/plain", output_buffer);
        return;
    }
    _add_output(ws, "Current app parition is first: OK!\n");

    // 2. copy partition table to local buffer
    _add_output(ws, "Reading partition table...\n");
    char *partition_buffer = (char *)malloc(SPI_FLASH_SEC_SIZE+1);
    //uint8_t partition_buffer[SPI_FLASH_SEC_SIZE];
    _my_esp_partition_t *partitions[10];
    esp_err_t err =
        spi_flash_read(PARTITION_TABLE_ADDRESS, partition_buffer, 1024);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to read partition table: 0x%x\n", err);
        _add_output(c_buffer);
        request->send(500, "text/plain", output_buffer);
        return;
    }
    _add_output(ws, "Done reading.\n");

    // separate out partitions
    _add_output(ws, "Splitting partitions out...\n");
    int partition_count = 0;
    bool any_errors = false;
    for (uint32_t offset = 0; offset < SPI_FLASH_SEC_SIZE; offset += 32) {
        if ((*(partition_buffer+offset)==0xAA) && (*(partition_buffer+offset+1)==0x50)) {
            partitions[partition_count] = (_my_esp_partition_t*)(partition_buffer + offset);
            partition_count++;
            if (partition_count>10) break;
        }
    }
    _add_output(ws, "Created local copy of partiton table: OK\n");
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "Type: %02x, Subtype: %02x, Addr: 0x%08x, Size: 0x%08x (%dK), Label: %s\n",
                partitions[i]->type, partitions[i]->subtype, partitions[i]->address, partitions[i]->size, (int)(partitions[i]->size/1024), partitions[i]->label);
        _add_output(c_buffer);
    }

    // 3. confirm that order is app, app, data; else fail
    bool is_good = false;
    int app0_index = -1, app1_index = -1, data_index = -1;
    for (int i=0; i<partition_count; i++) {
        snprintf(c_buffer, sizeof(c_buffer), "... Checking index %d, Label: %s\n",
                i, partitions[i]->label);
        _add_output(c_buffer);  
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
            _add_output(c_buffer);
            any_errors = true;
            break;
        }
    }
    if (!is_good) {
        _add_output(ws, "ERROR: partition table does not have app, app, data order.\n");
        any_errors = true;
    }
    if (any_errors) {
        request->send(500, "text/plain", output_buffer);
        return;
    }
    _add_output(ws, "Partition order is app, app, data: OK\n");

    // check if app actually needs resizing
    if (partitions[app1_index]->size >= IDEAL_APP_PARTITION_SIZE && 
        partitions[app0_index]->size >= IDEAL_APP_PARTITION_SIZE) {
        _add_output(ws, "UNNECESSARY: App partitions are already ideal size: OK\n");
        request->send(200, "text/plain", output_buffer);
        return;
    }
    // check if data partition is large enough
    if (partitions[data_index]->size >= ( 
        (partitions[app1_index]->size - IDEAL_APP_PARTITION_SIZE) +
        (partitions[app0_index]->size - IDEAL_APP_PARTITION_SIZE) ) ) {
        _add_output(ws, "ERROR: Data partition is not large enough to accomodate new app partitions.\n");
        request->send(500, "text/plain", output_buffer);
        return;
    }
    _add_output(ws, "Data partition is large enough to accomodate new app partitions: OK\n");

    // 4. freeze processing
    WLED::instance().disableWatchdog();
    usermods.onUpdateBegin(true); // notify usermods that update is about to begin (some may require task de-init)

    // 5. calculate new data partition size
    uint32_t new_data_size = (partitions[data_index]->size - 
        ((IDEAL_APP_PARTITION_SIZE - partitions[app1_index]->size) +
         (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size)) );
    uint32_t new_data_address = partitions[data_index]->address + 
        (IDEAL_APP_PARTITION_SIZE - partitions[app1_index]->size) +
        (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size);
    uint32_t new_app1_address = partitions[app1_index]->address + 
        (IDEAL_APP_PARTITION_SIZE - partitions[app0_index]->size);
    snprintf(c_buffer, sizeof(c_buffer), "Old: app0 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app0_index]->address, partitions[app0_index]->size, (int)(partitions[app0_index]->size/1024));
    _add_output(c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "Old: app1 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app1_index]->address, partitions[app1_index]->size, (int)(partitions[app1_index]->size/1024));
    _add_output(c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "Old: data partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[data_index]->address, partitions[data_index]->size, (int)(partitions[data_index]->size/1024));
    _add_output(c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: app0 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            partitions[app0_index]->address, IDEAL_APP_PARTITION_SIZE, (int)(IDEAL_APP_PARTITION_SIZE/1024));
    _add_output(c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: app1 partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_app1_address, IDEAL_APP_PARTITION_SIZE, (int)(IDEAL_APP_PARTITION_SIZE/1024));
    _add_output(c_buffer);
    snprintf(c_buffer, sizeof(c_buffer), "New: data partition address: 0x%08x, size: 0x%08x (%dK)\n",
            new_data_address, new_data_size, (int)(new_data_size/1024));
    _add_output(c_buffer);
    if (new_data_address - partitions[data_index]->address < SPI_FLASH_SEC_SIZE) {
        _add_output(ws, "ERROR: New data partition address offset by at least a sector.\n");
        request->send(500, "text/plain", output_buffer);
    }

    // 6. shift data partition in sector-chunks to new location
    // move in reverse order (last to first)
    _add_output(ws, "Shifting data partition to new location...\n");
    uint8_t shift_buffer[SPI_FLASH_SEC_SIZE+1];
    _add_output(ws, "2...\n");

    for (uint32_t offset = new_data_size - SPI_FLASH_SEC_SIZE; offset >= 0; offset -= SPI_FLASH_SEC_SIZE) {
        esp_err_t err = spi_flash_read(partitions[data_index]->address + offset, shift_buffer, SPI_FLASH_SEC_SIZE);
        if (err != ESP_OK) {
            snprintf(c_buffer, sizeof(c_buffer), "Address: 0x%08x\n", partitions[data_index]->address + offset);
            _add_output(c_buffer);
            snprintf(c_buffer, sizeof(c_buffer), "Failed to read data partition: 0x%x\n", err);
            _add_output(c_buffer);
            request->send(500, "text/plain", output_buffer);
            return;
        }
        err = spi_flash_erase_range(new_data_address + offset, SPI_FLASH_SEC_SIZE);
        if (err != ESP_OK) {
            snprintf(c_buffer, sizeof(c_buffer), "Address: 0x%08x\n", partitions[data_index]->address + offset);
            _add_output(c_buffer);
            snprintf(c_buffer, sizeof(c_buffer), "Failed to erase data partition: 0x%x\n", err);
            _add_output(c_buffer);
            request->send(500, "text/plain", output_buffer);
            return;
        }
        err = spi_flash_write(new_data_address + offset, shift_buffer, SPI_FLASH_SEC_SIZE);
        if (err != ESP_OK) {
            snprintf(c_buffer, sizeof(c_buffer), "Address: 0x%08x\n", partitions[data_index]->address + offset);
            _add_output(c_buffer);
            snprintf(c_buffer, sizeof(c_buffer), "Failed to write data partition: 0x%x\n", err);
            _add_output(c_buffer);
            request->send(500, "text/plain", output_buffer);
            return;
        }
    }
    _add_output(ws, "Data partition shifted to new location: OK\n");

    // 7. update data partition address and size in partition table
    // 8. update app1 partition address and size in partition table
    // 9. update app0 partition size in partition table
    partitions[data_index]->address = new_data_address;
    partitions[data_index]->size = new_data_size;
    partitions[app1_index]->address = new_app1_address;
    partitions[app1_index]->size = IDEAL_APP_PARTITION_SIZE;
    partitions[app0_index]->size = IDEAL_APP_PARTITION_SIZE;
    // write partition table buffer back to flash
    err = spi_flash_erase_range(PARTITION_TABLE_ADDRESS, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to erase partition table: 0x%x\n", err);
        _add_output(c_buffer);
        request->send(500, "text/plain", output_buffer);
        return;
    }
    err = spi_flash_write(PARTITION_TABLE_ADDRESS, partition_buffer, SPI_FLASH_SEC_SIZE);
    if (err != ESP_OK) {
        snprintf(c_buffer, sizeof(c_buffer), "Failed to write partition table: 0x%x\n", err);
        _add_output(c_buffer);
        request->send(500, "text/plain", output_buffer);
        return;
    }
    _add_output(ws, "Partition table rewritten: OK\n");

    // 10. update partition table checksum // lol, how
    // 11. thoughts and prayers. restore processing. reboot.
    WLED::instance().enableWatchdog();
    _add_output(ws, "Partition table updated. Rebooting...");

    // send results
    request->send(200, "text/plain", output_buffer);
    doReboot = true;
}

*/