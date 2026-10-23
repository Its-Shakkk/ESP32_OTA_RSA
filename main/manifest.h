#ifndef MANIFEST_H
#define MANIFEST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char version[16];
    char url[128];
    size_t size;
    char sha256[65];
    char hw_rev[8];
    char min_bootloader[16];
    uint32_t secure_version;
    bool mandatory;
    char project[32];
    char board[32];
    char signature[512];
} manifest_t;

#endif