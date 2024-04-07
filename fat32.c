#include "fat32.h"
#include "sdcard.h"

uint8_t fat32_sectors_per_cluster = 0;
uint32_t fat32_root_cluster = 2;
uint32_t fat32_fat_start = 0;
uint32_t fat32_data_start = 0;

static uint8_t _trim_space(char *str, uint8_t len) {
    while (str[--len] == ' ')
        str[len] = 0;
    return len + 1;
}

/**
 * @param dest needs to be a NUL initlized array of minimum 13 chars.
 * @param src is a pointer to the filename of a Fat32Entry. */
static void _copy_name(char *dest, const char *src) {
    memcpy(dest, src, 8);
    uint8_t len = _trim_space(dest, 8);
    if (src[8] != ' ') { 
        dest[len++] = '.';
        memcpy(dest + len, src + 8, 3);
        _trim_space(dest, len + 3);
    }
}

/**
 * @param dest is a pointer to the filename of a Fat32Entry. 
 * @param src needs to be a NUL initlized array of maximum 13 chars.
 * @return false if the filename is too long. */
static bool _rev_copy_name(char *dest, const char *src) {
    memset(dest, ' ', 8 + 3);
    uint8_t len = strlen(src);
    if (len > 13)
        return false;
    uint8_t c = len - 1;
    uint8_t dot = 0;
    uint8_t pos;
    while (c) {
        if (src[c] == '.') {
            pos = c;
            dot++;
            if (dot > 1) 
                break;
        }
        c--;
    }
    
    if (dot > 1)
        return false;

    if (dot == 0) {
        if (len > 8)
            return false;
        memcpy(dest, src, len);
    } else {
        memcpy(dest, src, pos);
        memcpy(dest + 8, src + pos + 1, len - pos - 1);
    }
    return true;
}


static bool _read_sector(uint32_t sector, uint8_t *data) {
    uint8_t tries = READ_SECTOR_TRIES;
    while(!sdcard_read_sector(sector, data))
        if (!tries--)
            return false;
    return true;
}

Fat32Error fat32_mount(void) {
    if (!sdcard_ready)
        return FAT32_NO_SDCARD;
    if (!_read_sector(0, sdcard_sector))
        return FAT32_GENERIC_SD_ERROR;
    const uint16_t boot_sig = *(uint16_t*) (sdcard_sector + SD_SECTOR_SIZE - 2);
    if (boot_sig != BOOT_SIGNATURE)
        return FAT32_NOT_FAT32;

    /* Search partitions */
    PartitionTable *pt = (PartitionTable *)(sdcard_sector
                                            + PARTITION_TABLE_OFFSET);

    uint32_t start_sector = pt->start_sector;
    
    bool found = false;
    for (uint8_t i = 0; i < 4; ++i) {
        if (pt->partition_type == FAT32_PT_TYPE) {
            found = true;
            break;
        }
        pt += sizeof (PartitionTable);
    }

    if (!found)
        return FAT32_NOT_FAT32;

    if (!_read_sector(pt->start_sector, sdcard_sector))
        return FAT32_GENERIC_SD_ERROR;

    Fat32BootSector *bsect = (Fat32BootSector *) sdcard_sector;

    fat32_sectors_per_cluster = bsect->sectors_per_cluster;
    fat32_fat_start = bsect->reserved_sectors + start_sector;
    fat32_data_start = fat32_fat_start +
        bsect->fat_size_sectors * bsect->number_of_fats;
    fat32_root_cluster = bsect->cluster_num_for_root;

    return FAT32_OK;
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    uint32_t sector = fat32_fat_start + cluster / (SD_SECTOR_SIZE / 4);
    if(!_read_sector(sector, sdcard_sector))
        return -1;
    cluster %= SD_SECTOR_SIZE / 4;
    uint32_t (*fat)[SD_SECTOR_SIZE / 4] = (uint32_t (*)[SD_SECTOR_SIZE / 4])
        sdcard_sector;
    return (*fat)[cluster];
}

uint32_t fat32_claim_free_cluster(void) {
    uint32_t sector = fat32_fat_start;
    uint32_t (*fat)[SD_SECTOR_SIZE / 4] = (uint32_t (*)[SD_SECTOR_SIZE / 4])
        sdcard_sector;

    uint32_t i = fat32_root_cluster + 1;
    _read_sector(sector, sdcard_sector);
    while (!IS_FREE_CLUSTER((*fat)[i % (SD_SECTOR_SIZE / 4)])) {
        i++;
        if (i % (SD_SECTOR_SIZE / 4) == 0) {
            _read_sector(++sector, sdcard_sector);
        }
    }
    (*fat)[i % (SD_SECTOR_SIZE / 4)] = 0xffffffff;
    sdcard_write_sector(sector, sdcard_sector);
    return i;
}

Fat32Error fat32_link_clusters(uint32_t head, uint32_t tail) {
    uint32_t sector = fat32_fat_start + head / (SD_SECTOR_SIZE / 4);
    if(!_read_sector(sector, sdcard_sector))
        return FAT32_GENERIC_SD_ERROR;
    head %= SD_SECTOR_SIZE / 4;
    uint32_t (*fat)[SD_SECTOR_SIZE / 4] = (uint32_t (*)[SD_SECTOR_SIZE / 4])
        sdcard_sector;
    (*fat)[head] = tail;
    sdcard_write_sector(sector, sdcard_sector);
    return FAT32_OK;
}

Fat32Error fat32_get_nth_file(Fat32File *file, uint32_t n) {
    file->exists = false;
    uint8_t sector = 0;
    uint32_t cluster = fat32_root_cluster;
    _read_sector(SECTOR(cluster, sector), sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    while (fs_entry->filename[0]) {
        /* Skip deleted / extension files. */
        if (fs_entry->filename[0] != '\xe5'
            && !IS_NAME_EXT(fs_entry->attributes)) {
            /* Check if we have arrived. */
            if (n-- == 0) {
                _copy_name(file->name, fs_entry->filename);
                file->exists = true;
                file->attr = fs_entry->attributes;
                file->file_size = fs_entry->file_size;
                file->starting_cluster = fs_entry->starting_cluster;
                file->entry_sector = SECTOR(cluster, sector);
                file->entry_offset =
                    ((uint8_t *)fs_entry - sdcard_sector) / sizeof (Fat32Entry);
                return FAT32_OK;
            }
        }

        /* Continue with next entry. */
        fs_entry++;

        /* Switch to next cluster/sector. */
        if ((uint8_t *) fs_entry >= (sdcard_sector + SD_SECTOR_SIZE)) {
            if (sector++ == fat32_sectors_per_cluster) {
                sector = 0;
                cluster = fat32_get_next_cluster(cluster);
                if (!IS_VALID_CLUSTER(cluster)) { /* We reached the end. */
                    return FAT32_INVALID_FILE;
                }
            }
            _read_sector(SECTOR(cluster, sector), sdcard_sector);
            fs_entry = (Fat32Entry *) sdcard_sector;
        }
    }
    return FAT32_INVALID_FILE;
}

Fat32Error fat32_find_file(Fat32File *file, const char *filename) {
    file->exists = false;
    uint8_t sector = 0;
    uint32_t cluster = fat32_root_cluster;
    _read_sector(SECTOR(cluster, sector), sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    while (fs_entry->filename[0]) {
        /* Skip garbage files... */
        if (fs_entry->filename[0] != '\xe5'
            && !IS_NAME_EXT(fs_entry->attributes)) {
            /* Check name of file... */
            _copy_name(file->name, fs_entry->filename);
            if (strcmp(file->name, filename) == 0) {
                /* Found file. */
                file->exists = true;
                file->attr = fs_entry->attributes;
                file->file_size = fs_entry->file_size;
                file->starting_cluster = fs_entry->starting_cluster;
                file->entry_sector = SECTOR(cluster, sector);
                file->entry_offset =
                    ((uint8_t *)fs_entry - sdcard_sector) / sizeof (Fat32Entry);
                return FAT32_OK;
            } else {
                /* File miss.  Clear the name we used for comparing. */
                memset(file->name, 0, sizeof (file->name));
            }
        }
        
        /* Next entry. */
        fs_entry++;

        /* Switch to next cluster/sector. */
        if ((uint8_t *) fs_entry >= (sdcard_sector + SD_SECTOR_SIZE)) {
            if (sector++ == fat32_sectors_per_cluster) {
                sector = 0;
                cluster = fat32_get_next_cluster(cluster);
                if (!IS_VALID_CLUSTER(cluster)) { /* We reached the end. */
                    return FAT32_INVALID_FILE;
                }
            }
            _read_sector(SECTOR(cluster, sector), sdcard_sector);
            fs_entry = (Fat32Entry *) sdcard_sector;
        }
    }
    return FAT32_INVALID_FILE;
}

uint16_t fat32_read_file(Fat32File *file, char *buf, uint16_t len) {
    uint8_t sector = file->cursor / SD_SECTOR_SIZE;
    uint16_t offset = file->cursor % SD_SECTOR_SIZE;
    uint32_t cluster = file->starting_cluster +
        (sector / fat32_sectors_per_cluster);
    sector %= fat32_sectors_per_cluster;

    _read_sector(SECTOR(cluster, sector), sdcard_sector);

    if (len > (file->file_size - file->cursor)) {
        len = (file->file_size - file->cursor);
    }

    for (uint16_t i = 0; i < len; ++i) {
        if ((offset + (i % SD_SECTOR_SIZE)) == SD_SECTOR_SIZE) {
            sector++;
            offset = 0;
            if (sector == fat32_sectors_per_cluster) {
                cluster = fat32_get_next_cluster(cluster);
                if (!IS_VALID_CLUSTER(cluster))
                    return i;
                sector = 0;
            }
            _read_sector(SECTOR(cluster, sector), sdcard_sector);
        }
        buf[i] = sdcard_sector[offset + (i % SD_SECTOR_SIZE)];
        file->cursor++;
    }
    
    return len;
}

Fat32Error fat32_write_file(Fat32File *file, const char *buf, uint16_t len) {
    uint8_t sector = file->cursor / SD_SECTOR_SIZE;
    uint16_t offset = file->cursor % SD_SECTOR_SIZE;
    uint32_t cluster = file->starting_cluster +
        (sector / fat32_sectors_per_cluster);
    sector %= fat32_sectors_per_cluster;

    _read_sector(SECTOR(cluster, sector), sdcard_sector);

    for (uint16_t i = 0; i < len; ++i) {
        if ((offset + (i % SD_SECTOR_SIZE)) == SD_SECTOR_SIZE) {
            sdcard_write_sector(SECTOR(cluster, sector),
                                sdcard_sector);
            sector++;
            offset = 0;
            if (sector == fat32_sectors_per_cluster) {
                uint32_t next_cluster = fat32_get_next_cluster(cluster);
                if (!IS_VALID_CLUSTER(next_cluster)) {
                    next_cluster = fat32_claim_free_cluster();
                    fat32_link_clusters(cluster, next_cluster);
                }
                cluster = next_cluster;
                sector = 0;
            }
            _read_sector(SECTOR(cluster, sector), sdcard_sector);
        }
        sdcard_sector[offset + (i % SD_SECTOR_SIZE)] = buf[i];
        file->cursor++;
    }
    sdcard_write_sector(SECTOR(cluster, sector), sdcard_sector);
    /* Update size */
    _read_sector(file->entry_sector, sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    fs_entry += file->entry_offset;
    fs_entry->file_size = file->file_size += len;
    sdcard_write_sector(file->entry_sector, sdcard_sector);
    return FAT32_OK;
}

Fat32Error fat32_delete_file(Fat32File *file) {
    uint32_t cluster = file->starting_cluster;
    uint32_t next_cluster;
    do {
        uint32_t sector = fat32_fat_start + cluster / (SD_SECTOR_SIZE / 4);
        uint8_t offset = cluster % (SD_SECTOR_SIZE / 4);
        _read_sector(sector, sdcard_sector);
        uint32_t (*fat)[SD_SECTOR_SIZE / 4] = (uint32_t (*)[SD_SECTOR_SIZE / 4])
            sdcard_sector;
        next_cluster = (*fat)[offset];
        (*fat)[offset] = 0;       /* mark free */
        sdcard_write_sector(sector, sdcard_sector);
        cluster = next_cluster;
    } while (IS_VALID_CLUSTER(next_cluster));
    _read_sector(file->entry_sector, sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    fs_entry += file->entry_offset;
    fs_entry->filename[0] = '\xe5'; /* mark as unused */
    sdcard_write_sector(file->entry_sector, sdcard_sector);
    file->exists = false;
    return FAT32_OK;
}

Fat32Error fat32_create_file(Fat32File *file, const char *name) {
    uint8_t sector = 0;
    uint32_t cluster = fat32_root_cluster;
    _read_sector(SECTOR(cluster, sector), sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    while (fs_entry->filename[0] != 0 &&
           fs_entry->filename[0] != '\xe5') {
        fs_entry++;
        /* Switch to next cluster/sector. */
        if ((uint8_t *) fs_entry >= (sdcard_sector + SD_SECTOR_SIZE)) {
            if (sector++ == fat32_sectors_per_cluster) {
                sector = 0;
                cluster = fat32_get_next_cluster(cluster);
                /* Allocate new cluster for root directory. */
                if (!IS_VALID_CLUSTER(cluster)) {
                    uint32_t next_cluster = fat32_claim_free_cluster();
                    fat32_link_clusters(cluster, next_cluster);
                    cluster = next_cluster;
                }
            }
            _read_sector(SECTOR(cluster, sector), sdcard_sector);
            fs_entry = (Fat32Entry *) sdcard_sector;
        }
    }

    uint32_t file_cluster = fat32_claim_free_cluster();
    _read_sector(SECTOR(cluster, sector), sdcard_sector);
    Fat32EntryAttr attr;
    attr.bits = 0;
    file->starting_cluster = fs_entry->starting_cluster = file_cluster;
    file->file_size = fs_entry->file_size = 0;
    fs_entry->modify_date = 0;
    fs_entry->modify_time = 0;
    file->attr = fs_entry->attributes = attr;
    memcpy(file->name, name, strlen(name));
    memset(fs_entry->reserved, 0, sizeof (fs_entry->reserved));
    memset(fs_entry->filename, ' ', 8 + 3);
    if (!_rev_copy_name(fs_entry->filename, name)) {
        return FAT32_FILENAME_ERROR;
    }
    sdcard_write_sector(SECTOR(cluster, sector), sdcard_sector);
    file->entry_sector = SECTOR(cluster, sector);
    file->entry_offset =
        ((uint8_t *)fs_entry - sdcard_sector) / sizeof (Fat32Entry);
    file->exists = true;
    file->cursor = 0;
    return FAT32_OK;
}

Fat32Error fat32_rename_file(Fat32File *file, const char *new_name) {
    _read_sector(file->entry_sector, sdcard_sector);
    Fat32Entry *fs_entry = (Fat32Entry *) sdcard_sector;
    fs_entry += file->entry_offset;
    if (!_rev_copy_name(fs_entry->filename, new_name))
        return FAT32_FILENAME_ERROR;
    sdcard_write_sector(file->entry_sector, sdcard_sector);
    memcpy(file->name, new_name, strlen(new_name));
    return FAT32_OK;
}

