#include "utils.h"

// dump a memory block to a char[] as hex, for debugging
void hex_dump(char *output, int max_len, const uint8_t *data, unsigned int data_len) {
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