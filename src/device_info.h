#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <stddef.h>

void getDeviceInfo(char* info, size_t infoSize);
void getBootloaderMd5(char *output_buffer, size_t output_buffer_size);

#endif // DEVICE_INFO_H