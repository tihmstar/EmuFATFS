//
//  EmuFATFS.cpp
//  EmuFATFS
//
//  Created by tihmstar on 31.03.24.
//

#include "EmuFATFS.hpp"
#include "fatfs.h"

#include <ctype.h>

#ifdef DEBUG
#   define cretassure(cond, errstr ...) do{ if ((cond) == 0){err=__LINE__;printf(errstr); goto error;} }while(0)
#else
#   define cretassure(cond, errstr ...) do{ if ((cond) == 0){err=__LINE__; goto error;} }while(0)
#endif

#define FAT16_THRESHOLD 65525 //https://github.com/dosfstools/dosfstools/blob/master/src/boot.c#L52

using namespace tihmstar;


#define BYTES_PER_SECTOR 512
#define SECTORS_PER_CLUSTER 128 //128 max allowed value

#define RESERVED_SECTORS_CNT 1
#define SECTORS_PER_FAT (0x20000 / BYTES_PER_SECTOR)
#define SECTORS_PER_ROOT_DIRECTORY (0x10000*2 / BYTES_PER_SECTOR)

#define SECTOR_BOOTSECTOR       (0)
#define SECTOR_FAT_1            (RESERVED_SECTORS_CNT)
#define SECTOR_FAT_2            (RESERVED_SECTORS_CNT + SECTORS_PER_FAT)
#define SECTOR_ROOT_DIRECTORY   (SECTOR_FAT_2 + SECTORS_PER_FAT)
#define SECTOR_DATA_REGION      (SECTOR_ROOT_DIRECTORY + SECTORS_PER_ROOT_DIRECTORY)

#define BYTES_PER_CLUSTER (BYTES_PER_SECTOR*SECTORS_PER_CLUSTER)
#define FIRST_DATA_CLUSTER      2

#define TOTAL_SECTORS (FAT16_THRESHOLD+(4/*boot sector + 2x fat table + directory entry*/))*SECTORS_PER_CLUSTER

#pragma mark helpers
static uint8_t lfn_checksum(const char *filename){
  uint8_t ret = filename[0];
  size_t filenamelen = 11;
  for (int i = 1; i < filenamelen; i++){
    ret = (ret >> 1) | (ret << 7);
    ret += filename[i];
  }
  return ret;
}


#pragma mark EmuFATFS
EmuFATFSBase::EmuFATFSBase(FileEntry *fileStorage, uint16_t maxFileStorageEntires, char *filenamesBuf, size_t filenamesBufSize, const char *volumeLabel)
: _fileStorage{fileStorage}, _maxFileStorageEntires{maxFileStorageEntires}, _usedFiles{0}
, _filenamesBuf{filenamesBuf}, _filenamesBufSize{filenamesBufSize}, _usedFilenamesBytes{0}
, _volumeLabel{}, _nextFreeCluster{FIRST_DATA_CLUSTER}
{
    memset(_volumeLabel, ' ', sizeof(_volumeLabel));
    if (volumeLabel){
        strncpy(_volumeLabel, volumeLabel, sizeof(_volumeLabel));
    }else{
        strncpy(_volumeLabel, "EmuFATFS16", sizeof(_volumeLabel));
    }
    
    for (int i=0; i<sizeof(_volumeLabel)-1; i++){
        _volumeLabel[i] = toupper(_volumeLabel[i]);
        if (_volumeLabel[i] == '\0'){
            _volumeLabel[i] = ' ';
            _volumeLabel[i+1] = '\0';
        }
    }
}

EmuFATFSBase::~EmuFATFSBase(){
    //
}

#pragma mark private

const EmuFATFSBase::FileEntry *EmuFATFSBase::getFileForSector(uint16_t sector){
    for (int i=0; i<_usedFiles; i++) {
        const FileEntry *cur = &_fileStorage[i];
        if (cur->f_read == NULL) break;
        uint16_t usedSectors = (cur->fileSize/BYTES_PER_SECTOR);
        if (cur->fileSize & (BYTES_PER_SECTOR-1)) usedSectors++;
        if (cur->startCluster+RESERVED_SECTORS_CNT <= sector && usedSectors > sector) {
            return cur;
        }
    }
    return NULL;
}

int32_t EmuFATFSBase::readFileAllocationTable(uint32_t offset, void *buf, uint32_t size){
    int err = 0;
    int32_t didRead = 0;
    uint16_t *fe = (uint16_t*)buf;
    uint16_t findex = offset/2;

    cretassure((size & 1) == 0, "read size needs to be 2 bytes aligned!");
    cretassure((offset & 1) == 0, "offset needs to be 2 bytes aligned!");
    
#define putentry(idx, val) \
        if (findex == idx) \
            do { \
                if (size < sizeof(*fe)) goto error; \
                *fe++ = val; size -= 2; didRead+=2; findex++;\
            } while (0)
    
    
    putentry(0, 0xfff8);//FAT16 type  (boot sector)
    putentry(1, 0x8000);//FAT16 type  (volume label)

    for (int i=0; i<_usedFiles; i++) {
        const FileEntry *cur = &_fileStorage[i];
        if (cur->startCluster != findex) break;
        
        uint16_t usedSectors = (cur->fileSize/BYTES_PER_SECTOR);
        if (cur->fileSize & (BYTES_PER_SECTOR-1)) usedSectors++;

        while (usedSectors > 1) {
            if (size < sizeof(*fe)) goto error;
            *fe++ = ++findex; didRead+=2; size-=2;
        }
        if (usedSectors) {
            if (size < sizeof(*fe)) goto error;
            *fe++ = 0xFFFF; didRead+=2; size-=2;
            findex++;
        }
    }
    
    if (offset + size > SECTORS_PER_FAT*BYTES_PER_SECTOR) size = SECTORS_PER_FAT*BYTES_PER_SECTOR - didRead - offset;
    memset(fe, 0, size); didRead += size;
    
error:
    if (err) {
        return -err;
    }
    return didRead;
#undef putentry
}

int32_t EmuFATFSBase::readBootsector(uint32_t offset, void *buf, uint32_t size){
    int err = 0;
    int32_t didRead = 0;
    uint8_t *ptr = (uint8_t*)buf;
    FAT_Bootsector_t bs = {};
            
    *(uint32_t*)bs.jumpInsn = 0x00903ceb;
    memcpy(bs.oemName, "EmuFATFS", sizeof(bs.oemName));
    
    bs.dos4 = {
        .dos3 = {
            .dos2 = {
                .bytesPerSector = BYTES_PER_SECTOR,
                .sectorsPerCluster = SECTORS_PER_CLUSTER, //128 is maximum
                .reservedSectors = RESERVED_SECTORS_CNT,
                .numberOfFATs = 2, //keep this to "2" for compatibility reasons
                .rootdirectoryEntries = SECTORS_PER_FAT*BYTES_PER_SECTOR / 32,
                .totalSectors = 0, //more than 0x10000 sectors
                .mediaDescriptor = 0xF8, //"fixed disk" (i.e. partition on a hard drive)
                .sectorsPerFAT = SECTORS_PER_FAT,
            },
            .physSectorsPerTrack = 1,
            .numberOfHeads = 1,
            .hiddenSectors = 0,
            .largeTotalSectors = TOTAL_SECTORS,
        },
        .physDriveNumber = 0,
        .flags = 0,
        .extendedBootSignature = 0x29,
        .volumeSerialNumber = 0x6d686974,
        .volumeLabel = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
        .filesystemType = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '},
    };
    memcpy(&bs.dos4.volumeLabel, _volumeLabel, sizeof(bs.dos4.volumeLabel));
    bs.signature = 0xaa55;
    
    if (offset < sizeof(bs)) {
        size_t doCopy = sizeof(bs) - offset;
        if (doCopy > size) doCopy = size;
        memcpy(ptr, ((uint8_t*)&bs)+offset, doCopy);
        ptr += doCopy;
        size -= doCopy;
        didRead += doCopy;
    }
        
    if (offset + size > BYTES_PER_SECTOR) size = BYTES_PER_SECTOR - didRead - offset;
    
    memset(ptr, 0, size);
    didRead += size;

error:
    if (err) {
        return -err;
    }
    return didRead;
}

int32_t EmuFATFSBase::readRootDirectory(uint32_t offset, void *buf, uint32_t size){
    int err = 0;
    int32_t didRead = 0;
    uint8_t *ptr = (uint8_t*)buf;
    FAT_DirectoryTableFileEntry_t dfe = {};
    
    uint32_t processedEntries = 1;
    
#define DTINDEX (offset / sizeof(dfe))
#define MOVEOFFSET do {ptr += sizeof(FAT_DirectoryTableFileEntry_t); size -= sizeof(FAT_DirectoryTableFileEntry_t); didRead+=sizeof(FAT_DirectoryTableFileEntry_t); offset +=sizeof(FAT_DirectoryTableFileEntry_t);} while(0)
    
    cretassure((offset % sizeof(dfe)) == 0, "Partial entry reads are not handled");
    
    if (DTINDEX == 0) {
        if (size < sizeof(FAT_DirectoryTableEntry_t)) goto error;
        FAT_DirectoryTableLFNEntry_t *vle = (FAT_DirectoryTableLFNEntry_t *)ptr;
        snprintf((char*)vle, 13, "%s             ",_volumeLabel);
        vle->attributes = FILEENTRY_ATTR_VOLUME_LABEL;
        MOVEOFFSET;
    }
    
    for (int i=0; i<_usedFiles; i++) {
        const FileEntry *cfe = &_fileStorage[i];
        uint8_t neededExtraEntries = cfe->filenameLenNoSuffix+1;
        for (int j=0; j<3; j++) {
            if (cfe->filename[cfe->filenameLenNoSuffix+1+j] == ' ') break;
            neededExtraEntries++;
        }
        if (neededExtraEntries % LFN_ENTRY_MAX_NAME_LEN) neededExtraEntries += LFN_ENTRY_MAX_NAME_LEN;
        neededExtraEntries /= LFN_ENTRY_MAX_NAME_LEN;
        
        //first construct main entry
        dfe = {
            /*
                The following 3 members are not filled here, but in the code down below
             */
            .shortFilename = {},
            .filenameExt = {},
            .fileAttributes = {},
            /*
                Actual data
             */
            .reserved = 0,
            .createTime_ms = 0,
            .createTime = 0,
            .createDate = 0,
            .accessedDate = 0,
            .clusterNumber_High = static_cast<uint16_t>(cfe->startCluster>>16),
            .modifiedTime = 0,
            .modifiedDate = 0,
            .clusterLocation = static_cast<uint16_t>(cfe->startCluster),
            .fileSize = cfe->fileSize,
        };
        snprintf(dfe.shortFilename, 9, "%s        ",cfe->filename);
        if (cfe->filenameLenNoSuffix > 8) {
            snprintf(&dfe.shortFilename[5], 4, "\x7e%02d",i+1);
        }
        memcpy(dfe.filenameExt, &cfe->filename[cfe->filenameLenNoSuffix+1], 3);
        uint8_t csum = lfn_checksum(dfe.shortFilename);
        dfe.fileAttributes = FILEENTRY_ATTR_SYSTEM | (cfe->f_write == NULL ? FILEENTRY_ATTR_READONLY : 0);
        

        if (DTINDEX == processedEntries++){
            //first entry marking end of name
            FAT_DirectoryTableLFNEntry_t *lfn = (FAT_DirectoryTableLFNEntry_t*)ptr;
            if (size < sizeof(FAT_DirectoryTableEntry_t)) goto error;
            
            memset(lfn, 0xFF, sizeof(*lfn));
            lfn->sequenceNumber = neededExtraEntries | LFN_ENTRY_LAST;
            lfn->attributes = FILEENTRY_ATTR_LFN_ENTRY;
            lfn->type = 0;
            lfn->checksum = csum;
            lfn->zero = 0;
            
            const char *fnameend = cfe->filename + (neededExtraEntries-1)*LFN_ENTRY_MAX_NAME_LEN;
            ssize_t fnameendLen = 0;
            if (fnameend - cfe->filename > cfe->filenameLenNoSuffix) {
                fnameendLen = (ssize_t)cfe->filenameLenNoSuffix - (fnameend - cfe->filename);
                fnameend = NULL;
            }else{
                fnameendLen = strlen(fnameend);
            }

            for (int j = 0; j<LFN_ENTRY_MAX_NAME_LEN && j <=fnameendLen+3; j++) {
                char c = '\0';
                if (fnameend) {
                    c = *fnameend++;
                    if (!c){
                        c = '.';
                        fnameend = NULL;
                    }
                }else{
                    int suffixIdx = j - (int)(fnameendLen+1);
                    if (suffixIdx < 3) {
                        c = cfe->filename[cfe->filenameLenNoSuffix+1 + suffixIdx];
                    }
                    if (c == ' ') c = '\0';
                }
                
                if (j < 5) {
                    lfn->name1[j] = c;
                } else if (j < 5+6) {
                    lfn->name2[j-5] = c;
                } else {
                    lfn->name3[j-(5+6)] = c;
                }
                if (c == '\0') break;
            }
            MOVEOFFSET;
        }
        
        for (int z=neededExtraEntries-2; z>=0; z--) {
            if (DTINDEX == processedEntries++){
                FAT_DirectoryTableLFNEntry_t *lfn = (FAT_DirectoryTableLFNEntry_t*)ptr;
                if (size < sizeof(FAT_DirectoryTableEntry_t)) goto error;
                
                memset(lfn, 0xFF, sizeof(*lfn));
                lfn->sequenceNumber = z+1;
                lfn->attributes = FILEENTRY_ATTR_LFN_ENTRY;
                lfn->type = 0;
                lfn->checksum = csum;
                lfn->zero = 0;
                
                for (int j = 0; j<LFN_ENTRY_MAX_NAME_LEN; j++) {
                    char c = cfe->filename[j+z*LFN_ENTRY_MAX_NAME_LEN];
                    
                    if (j+z*LFN_ENTRY_MAX_NAME_LEN == cfe->filenameLenNoSuffix){
                        c = '.';
                    }
                    
                    if (j < 5) {
                        lfn->name1[j] = c;
                    } else if (j < 5+6) {
                        lfn->name2[j-5] = c;
                    } else {
                        lfn->name3[j-(5+6)] = c;
                    }
                }
                MOVEOFFSET;
            }
        }

        if (DTINDEX == processedEntries++){
            if (size < sizeof(FAT_DirectoryTableEntry_t)) goto error;
            memcpy(ptr, &dfe, sizeof(dfe));
            MOVEOFFSET;
        }
    }

    if (offset + size > SECTOR_ROOT_DIRECTORY*BYTES_PER_SECTOR) size = SECTOR_ROOT_DIRECTORY*BYTES_PER_SECTOR - didRead - offset;
    memset(ptr, 0, size); didRead += size;
    
error:
    if (err) {
        return -err;
    }
    return didRead;
#undef DTINDEX
}


#pragma mark public
#pragma mark host accessors
int32_t EmuFATFSBase::hostRead(uint32_t offset, void *buf, uint32_t size){
    uint8_t *ptr = (uint8_t*)buf;
    uint32_t sectorNum = offset / BYTES_PER_SECTOR;
    int32_t didRead = 0;
    
    if (sectorNum == SECTOR_BOOTSECTOR) {
        didRead = readBootsector(offset, buf, size);
        if (didRead > 0 && (didRead & (BYTES_PER_SECTOR-1)) == 0) return didRead;
        
    }else if (sectorNum >= SECTOR_FAT_1 && sectorNum < SECTOR_FAT_1 + SECTORS_PER_FAT) {
        uint32_t sectionOffset = offset - SECTOR_FAT_1*BYTES_PER_SECTOR;

        didRead = readFileAllocationTable(sectionOffset, buf, size);
        if (didRead > 0 && (didRead & (BYTES_PER_SECTOR-1)) == 0) return didRead;

    }else if (sectorNum >= SECTOR_FAT_2 && sectorNum < SECTOR_FAT_2 + SECTORS_PER_FAT) {
        uint32_t sectionOffset = offset - SECTOR_FAT_2*BYTES_PER_SECTOR;

        didRead = readFileAllocationTable(sectionOffset, buf, size);
        if (didRead > 0 && (didRead & (BYTES_PER_SECTOR-1)) == 0) return didRead;

    }else if (sectorNum >= SECTOR_ROOT_DIRECTORY && sectorNum < SECTOR_DATA_REGION) {
        uint32_t sectionOffset = offset - SECTOR_ROOT_DIRECTORY*BYTES_PER_SECTOR;

        didRead = readRootDirectory(sectionOffset, buf, size);
        if (didRead > 0 && (didRead & (BYTES_PER_SECTOR-1)) == 0) return didRead;
    }else if (sectorNum >= SECTOR_DATA_REGION) {
        uint32_t sectionOffset = offset - SECTOR_DATA_REGION*BYTES_PER_SECTOR;

        uint32_t cluster = sectionOffset / BYTES_PER_CLUSTER;
        
        for (int i=0; i<_usedFiles; i++) {
            const FileEntry *cfe = &_fileStorage[i];
            uint32_t fileStartCluster = cfe->startCluster - FIRST_DATA_CLUSTER;
            uint32_t fileClusterCnt = cfe->fileSize / BYTES_PER_CLUSTER;
            if (cfe->fileSize & (BYTES_PER_CLUSTER-1)) fileClusterCnt++;
            if (cluster >= fileStartCluster && cluster < fileStartCluster + fileClusterCnt) {
                uint32_t fileOffset = sectionOffset - cluster * BYTES_PER_CLUSTER;
                didRead = cfe->f_read(fileOffset, buf, size, cfe->filename);
                break;
            }
        }

        if (didRead < 0) didRead = 0;
        if (size > BYTES_PER_SECTOR*SECTORS_PER_CLUSTER) size = BYTES_PER_CLUSTER;
        memset(&ptr[didRead], 0, size-didRead);
        return size;
    }

    if (didRead < 0) didRead = 0;
    if (size > BYTES_PER_SECTOR) size = BYTES_PER_SECTOR;
    memset(&ptr[didRead], 0, size-didRead);
    return size;
}

int32_t EmuFATFSBase::hostWrite(uint32_t offset, const void *buf, uint32_t size){
    uint32_t sectorNum = offset / BYTES_PER_SECTOR;
    int32_t didWrite = 0;
    
    if (sectorNum >= SECTOR_DATA_REGION) {
        uint32_t sectionOffset = offset - SECTOR_DATA_REGION*BYTES_PER_SECTOR;

        uint32_t cluster = sectionOffset / BYTES_PER_CLUSTER;
        
        for (int i=0; i<_usedFiles; i++) {
            const FileEntry *cfe = &_fileStorage[i];
            if (!cfe->f_write) continue;
            
            uint32_t fileStartCluster = cfe->startCluster - FIRST_DATA_CLUSTER;
            uint32_t fileClusterCnt = cfe->fileSize / BYTES_PER_CLUSTER;
            if (cfe->fileSize & (BYTES_PER_CLUSTER-1)) fileClusterCnt++;
            if (cluster >= fileStartCluster && cluster < fileStartCluster + fileClusterCnt) {
                uint32_t fileOffset = sectionOffset - cluster * BYTES_PER_CLUSTER;
                didWrite = cfe->f_write(fileOffset, buf, size, cfe->filename);
                break;
            }
        }
    }

    return size;
}

uint32_t EmuFATFSBase::diskBlockNum(){
    return TOTAL_SECTORS;
}

uint32_t EmuFATFSBase::diskBlockSize(){
    return BYTES_PER_SECTOR;
}


#pragma mark emu providers
void EmuFATFSBase::resetFiles(){
    _usedFiles = 0;
    _usedFilenamesBytes = 0;
    _nextFreeCluster = FIRST_DATA_CLUSTER;
}

int EmuFATFSBase::addFile(const char *filename, const char *filenameSuffix, uint32_t fileSize, cb_read f_read, cb_write f_write){
    int err = 0;
    
    char *fnameDst = &_filenamesBuf[_usedFilenamesBytes];
    size_t fnameSize = _filenamesBufSize-_usedFilenamesBytes;
    size_t neededNameBytes = strlen(filename) + 1 + 3;

    cretassure(neededNameBytes <= fnameSize, "Not enough space to add filename");
    cretassure(_usedFiles < _maxFileStorageEntires, "Not enough file entries left");
    cretassure(f_read, "No read function provided");
    cretassure(fileSize, "Zero byte files are not supported");

    snprintf(fnameDst, neededNameBytes+1, "%s%c%s%s", filename, '\0', filenameSuffix ? filenameSuffix : "", "   ");

    {
        FileEntry *cfe = &_fileStorage[_usedFiles];
        cfe->f_read = f_read;
        cfe->f_write = f_write;
        cfe->filename = fnameDst;
        cfe->filenameLenNoSuffix = (uint32_t)strlen(fnameDst);
        cfe->fileSize = fileSize;
        cfe->startCluster = _nextFreeCluster;

        uint32_t neededClusters = fileSize / BYTES_PER_CLUSTER;
        if (fileSize & (BYTES_PER_CLUSTER -1)) neededClusters++;

        cretassure(cfe->startCluster + neededClusters < 0x10000, "Not enough sectors left to store file");
        
        _nextFreeCluster += neededClusters;
    }
    
    _usedFiles++;
    _usedFilenamesBytes += neededNameBytes;
    
error:
    return -err;
}
