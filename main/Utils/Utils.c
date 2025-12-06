#include "Utils.h"
#include <stdio.h>
#include <sys/time.h>

int utils_generate_uuid(char *uuid, size_t uuid_size) {
    if (uuid == NULL || uuid_size < 37) {
        return -1;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t random_part = (uint32_t)(tv.tv_usec ^ tv.tv_sec);
    
    snprintf(uuid, uuid_size, "%08x-%04x-%04x-%04x-%08x%04x",
             (unsigned int)(tv.tv_sec & 0xFFFFFFFF),
             (unsigned int)((tv.tv_usec >> 16) & 0xFFFF),
             (unsigned int)(random_part & 0xFFFF),
             (unsigned int)((random_part >> 16) & 0xFFFF),
             (unsigned int)(tv.tv_usec & 0xFFFFFFFF),
             (unsigned int)(random_part & 0xFFFF));

    return 0;
}

