//
//  EmuFATFS.hpp
//  EmuFATFS
//
//  Created by tihmstar on 31.03.24.
//

#ifndef EmuFATFS_hpp
#define EmuFATFS_hpp

#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace tihmstar {

class EmuFATFSBase {
public:
    typedef int32_t (*cb_read)(uint32_t offset, void *buf, uint32_t size, const char *filename);
    typedef int32_t (*cb_write)(uint32_t offset, const void *buf, uint32_t size, const char *filename);
    typedef void (*cb_newFile)(const char *filename, const char filenameSuffix[3], uint32_t fileSize, uint32_t clusterLocation);

    struct FileEntry{
        cb_read f_read;
        cb_write f_write;
        const char *filename;
        uint32_t filenameLenNoSuffix;
        uint32_t fileSize;
        uint32_t startCluster;
        bool isDynamicFile;
    };
    
private:
    FileEntry *_fileStorage;
    const uint16_t _maxFileStorageEntires;
    uint16_t _usedFiles;
    
    char *_filenamesBuf;
    const size_t _filenamesBufSize;
    size_t _usedFilenamesBytes;

    uint16_t _bytesPerSector;
    
    char _volumeLabel[12];
    uint16_t _nextFreeCluster;
    cb_newFile _newfilecb;

#ifdef XCODE
public:
#endif
#pragma mark private
    const FileEntry *getFileForSector(uint16_t sector);
    int32_t readFileAllocationTable(uint32_t offset, void *buf, uint32_t size);
    int32_t readBootsector(uint32_t offset, void *buf, uint32_t size);
    int32_t readRootDirectory(uint32_t offset, void *buf, uint32_t size);

    int32_t catchRootDirectoryAccess(uint32_t offset, const void *buf, uint32_t size);
    int32_t catchFileAllocationTableAccess(uint32_t offset, const void *buf, uint32_t size);

#ifndef XCODE
public:
#endif
#pragma mark public
    EmuFATFSBase(FileEntry *fileStorage, uint16_t maxFileStorageEntires, char *filenamesBuf, size_t filenamesBufSize, const char *volumeLabel = NULL, uint16_t bytesPerSector = 0x400);
    ~EmuFATFSBase();
    
#pragma mark host accessors
    int32_t hostRead(uint32_t offset, void *buf, uint32_t size);
    int32_t hostWrite(uint32_t offset, const void *buf, uint32_t size);
    
    uint32_t diskBlockNum();
    uint32_t diskBlockSize();
    uint32_t bytesPerCluster();

#pragma mark emu providers
    void resetFiles();
    int addFile(const char *filename, const char *filenameSuffix, uint32_t fileSize, cb_read f_read, cb_write f_write = NULL);
    int addFileDynamic(const char *filename, const char *filenameSuffix, uint32_t fileSize, uint32_t startCluster, cb_read f_read, cb_write f_write = NULL);
    void registerNewfileCallback(cb_newFile f_newfilecb);
};

template <uint16_t TMPL_max_Files = 5, size_t TMPL_filenames_storage_size = 0x100>
class EmuFATFS : public EmuFATFSBase{
    FileEntry _fileStorage[TMPL_max_Files];
    char _filenamesStorage[TMPL_filenames_storage_size];
public:
    EmuFATFS(const char *volumeLabel = NULL, uint16_t bytesPerSector = 0x400)
    : EmuFATFSBase(_fileStorage, TMPL_max_Files, _filenamesStorage, TMPL_filenames_storage_size, volumeLabel, bytesPerSector){
        memset(_fileStorage, 0, sizeof(_fileStorage));
        memset(_filenamesStorage, 0, sizeof(_filenamesStorage));
    }
    ~EmuFATFS() {
        //
    }
};

};

#endif /* EmuFATFS_hpp */
