#ifndef FAT32_LIB
#define FAT32_LIB

#include <stdint.h>

#define READ_SECTOR_TRIES 5
#define BOOT_SIGNATURE 0xaa55
#define PARTITION_TABLE_OFFSET 0x1be
#define FAT32_PT_TYPE 0x0b
#define IS_VALID_CLUSTER(C) ((C) != 0x00000000 && (C) < 0x0ffffff7)
#define IS_FREE_CLUSTER(C) ((C) == 0x00000000)
#define IS_NAME_EXT(A) ((A).read_only && (A).hidden && (A).system       \
                        && (A).volume_id)
#define SECTOR(C, S) (fat32_data_start +     \
                      (fat32_sectors_per_cluster * ((C) - 2)) + (S))

typedef struct {
    uint8_t first_byte;
    uint8_t start_chs[3];
    uint8_t partition_type;
    uint8_t end_chs[3];
    uint32_t start_sector;
    uint32_t length_sectors;
} __attribute__ ((packed)) PartitionTable;

typedef struct {
    uint8_t _jmp[3];
    char oem[8];
    uint16_t sector_size;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint16_t _root_dir_entries;   /* N.A. for FAT32. */
    uint16_t total_sectors_u16;   /* 0 if partition > 32MB. */
    uint8_t media_descriptor;
    uint16_t _fat_size_sectors;   /* N.A. for FAT32. */
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_u32;   /* If u16 is 0, use this instead. */
    /* FAT32 only */
    uint32_t fat_size_sectors;
    uint16_t fat_flags;
    uint16_t fat_fs_version;
    uint32_t cluster_num_for_root;
    uint16_t sector_fsinfo;
    uint16_t sector_boot_bkup;
    uint8_t __reserved[12];
    /* FAT32 only end */
    uint8_t drive_number;
    uint8_t current_head;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
    char boot_code[420];
    uint16_t boot_sector_signature;
} __attribute__ ((packed)) Fat32BootSector;


typedef union {
    uint8_t bits;
    struct {
        unsigned read_only : 1;
        unsigned hidden : 1;
        unsigned system : 1;
        unsigned volume_id : 1;
        unsigned directory : 1;
        unsigned archive : 1;
        unsigned __reserved1 : 1;
        unsigned __reserved2 : 1;
    };
} __attribute__ ((packed)) Fat32EntryAttr;

typedef struct {
    char filename[8];
    char ext[3];
    Fat32EntryAttr attributes;
    uint8_t reserved[10];
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t starting_cluster;
    uint32_t file_size;
} __attribute__ ((packed)) Fat32Entry;

typedef enum {
    FAT32_OK = 0,
    FAT32_NO_SDCARD,
    FAT32_GENERIC_SD_ERROR,
    FAT32_NOT_FAT32,
    FAT32_INVALID_FILE,
    FAT32_FILENAME_ERROR,
    FAT32_FS_ERROR
} Fat32Error;

/* 32 bytes per File. */
typedef struct {
    bool exists;
    char name[13]; /* name + extension + period + NUL */
    Fat32EntryAttr attr;
    uint32_t starting_cluster;
    uint32_t file_size;
    uint32_t cursor;
    uint32_t entry_sector;
    uint8_t entry_offset;
} Fat32File;

Fat32Error fat32_mount(void);

Fat32Error fat32_get_nth_file(Fat32File *file, uint32_t n);

uint32_t fat32_get_next_cluster(uint32_t cluster);

Fat32Error fat32_find_file(Fat32File *file, const char *filename);

uint16_t fat32_read_file(Fat32File *file, char *buf, uint16_t len);

uint32_t fat32_claim_free_cluster(void);

Fat32Error fat32_link_clusters(uint32_t head, uint32_t tail);

Fat32Error fat32_write_file(Fat32File *file, const char *buf, uint16_t len);

Fat32Error fat32_delete_file(Fat32File *file);

Fat32Error fat32_create_file(Fat32File *file, const char *name);

Fat32Error fat32_rename_file(Fat32File *file, const char *new_name);

extern uint8_t fat32_sectors_per_cluster;
extern uint32_t fat32_root_cluster;
extern uint32_t fat32_fat_start;
extern uint32_t fat32_data_start;

#endif /* FAT32_LIB */
