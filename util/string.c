#include "util/string.h"

void strncpy(char *dest, const char *src, long len) {
    long i = 0;
    while (i < len - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int strlen(const char *str) {
    int i = 0;
    while (str[i] != '\0') {
        i++;
    }
    return i;
}
