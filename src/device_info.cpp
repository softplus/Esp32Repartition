#include "device_info.h"
#include "part_mgr.h"
#include "main.h"

#include <Arduino.h>
#include "Esp.h"
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <MD5Builder.h>

// from Esp.cpp
uint32_t ESP_getFlashChipId(void);

// wrapper for ESP32/ESP8266
uint32_t _ESP_getFlashChipSize(void) {
#ifdef ESP32
  return ESP.getFlashChipSize();
#endif // ESP32
#ifdef ESP8266
  return ESP.getFlashChipRealSize();
#endif // ESP8266
  return 0;
}

void getDeviceInfo(char* info, size_t infoSize) {
    uint32_t chipId = 0;
    for(int i=0; i<17; i=i+8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }

    snprintf(info, infoSize,
             "Build: %s %s SDK %s / %u (%s)\n"
             "Flash chip ID / Size: 0x%x / %u KB\n"
             "Program heap / program size: %u KB / %u KB",
             __DATE__, __TIME__,
             ESP.getSdkVersion(), chipId, ESP.getChipModel(),
             ESP_getFlashChipId(), _ESP_getFlashChipSize()/1024,
             ESP.getHeapSize()/1024, ESP.getSketchSize()/1024
             );
}

// get MD5 of bootloader
void getBootloaderMd5(char *output_buffer, size_t output_buffer_size) {
  if (output_buffer_size < (ESP_ROM_MD5_DIGEST_LEN * 2 + 4)) {
    snprintf(output_buffer, output_buffer_size, "Buffer too small\n");
    return;
  }
  char md5_buf[ESP_ROM_MD5_DIGEST_LEN];
  MD5Builder _md5 = MD5Builder();
  _md5.begin();

  uint8_t partition_buffer[SPI_FLASH_SEC_SIZE];
  for (uint32_t addr = 0x1000; addr < getPartitionTableAddr(); addr += SPI_FLASH_SEC_SIZE) {
    if (spi_flash_read(addr, partition_buffer, SPI_FLASH_SEC_SIZE) != ESP_OK) {
      snprintf(output_buffer, output_buffer_size, "Failed to read flash at offset 0x%x\n", addr);
      DEBUG_PRINT(output_buffer);
      return;
    }
    _md5.add(partition_buffer, SPI_FLASH_SEC_SIZE);
  }
  
  _md5.calculate();
  _md5.getBytes((uint8_t*)(md5_buf));

  // Get hex version of MD5, format kinda
  for (int i = 0; i < 16; i++) {
    snprintf(output_buffer + i * 2 + (i / 4), output_buffer_size - i * 2 - (i / 4), "%02x", (uint8_t)md5_buf[i]);
    if ((i + 1) % 4 == 0 && i != 15) {
      output_buffer[(i + 1) * 2 + (i / 4)] = ' ';
    }
  }
  output_buffer[32 + 3] = '\0'; // Null-terminate the string
}
