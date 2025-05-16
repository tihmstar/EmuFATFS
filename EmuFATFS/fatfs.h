//
//  fatfs.h
//  EmuFATFS
//
//  Created by tihmstar on 31.03.24.
//

#ifndef fatfs_h
#define fatfs_h

#include <stdint.h>

#define ATTRIBUTE_PACKED __attribute__ ((packed))

typedef struct {
    uint16_t    bytesPerSector;
    uint8_t     sectorsPerCluster;
    uint16_t    reservedSectors;
    uint8_t     numberOfFATs;
    uint16_t    rootdirectoryEntries;
    uint16_t    totalSectors;
    uint8_t     mediaDescriptor;
    uint16_t    sectorsPerFAT;
} ATTRIBUTE_PACKED FAT_BPB_DOS2_00_t;


typedef struct {
    FAT_BPB_DOS2_00_t   dos2;
    uint16_t            physSectorsPerTrack;
    uint16_t            numberOfHeads;
    uint32_t            hiddenSectors;
    uint32_t            largeTotalSectors;
} ATTRIBUTE_PACKED FAT_BPB_DOS3_31_t;

typedef struct {
    FAT_BPB_DOS3_31_t   dos3;
    uint8_t             physDriveNumber;
    uint8_t             flags;
    uint8_t             extendedBootSignature;
    uint32_t            volumeSerialNumber;
    char                volumeLabel[11];
    char                filesystemType[8];
} ATTRIBUTE_PACKED FAT_BPB_DOS4_00_t;

typedef struct{
    uint8_t jumpInsn[3]; //eb 3c 90
    char oemName[8];
    union {
        FAT_BPB_DOS4_00_t dos4;
        uint8_t __pad[0x1f3];
    };
    uint16_t signature; //0xaa55 
} ATTRIBUTE_PACKED FAT_Bootsector_t;


typedef struct{
    uint16_t day  : 5;
    uint16_t mon  : 4;
    uint16_t year : 7;
} ATTRIBUTE_PACKED FatDate_t;

typedef struct{
    uint16_t sec  : 5;
    uint16_t min  : 6;
    uint16_t hour : 5;
} ATTRIBUTE_PACKED FatTime_t;

typedef struct{
    char shortFilename[8];
    char filenameExt[3];
    uint8_t fileAttributes;
    uint8_t  reserved;
    uint8_t  createTime_ms;
    FatTime_t createTime;
    FatDate_t createDate;
    FatDate_t accessedDate;
    uint16_t clusterNumber_High;
    FatTime_t modifiedTime;
    FatDate_t modifiedDate;
    uint16_t clusterLocation;
    uint32_t fileSize;
} ATTRIBUTE_PACKED FAT_DirectoryTableFileEntry_t;


typedef struct{
    uint8_t sequenceNumber;
    char16_t name1[5];
    uint8_t attributes; //always 0x0F
    uint8_t type; //always 0x00
    uint8_t checksum;
    char16_t name2[6];
    uint16_t zero; //always 0x0000
    char16_t name3[2];
} ATTRIBUTE_PACKED FAT_DirectoryTableLFNEntry_t;

typedef union {
    FAT_DirectoryTableFileEntry_t   dfe;
    FAT_DirectoryTableLFNEntry_t    lfn;
} ATTRIBUTE_PACKED FAT_DirectoryTableEntry_t;

#define FILEENTRY_ATTR_READONLY     (1 << 0)
#define FILEENTRY_ATTR_HIDDEN       (1 << 1)
#define FILEENTRY_ATTR_SYSTEM       (1 << 2) //means: do not move this file physically (during defrag)!
#define FILEENTRY_ATTR_VOLUME_LABEL (1 << 3)
#define FILEENTRY_ATTR_SUBDIR       (1 << 4)
#define FILEENTRY_ATTR_ARCHIVE      (1 << 5)
#define FILEENTRY_ATTR_LFN_ENTRY    0x0F

#define LFN_ENTRY_LAST     0x40
#define LFN_ENTRY_DELETED  0x80
#define LFN_ENTRY_MAX_NAME_LEN 13

#endif /* fatfs_h */
