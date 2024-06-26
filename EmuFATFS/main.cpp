//
//  main.cpp
//  EmuFATFS
//
//  Created by tihmstar on 31.03.24.
//

#include "EmuFATFS.hpp"

#include <libgeneral/macros.h>

#include <stdio.h>


int32_t info_read_cb(uint32_t foffset, void *buf, uint32_t bufSize, const char *filaneme){
    char content[] = "Hello world file!!";
    if (bufSize > sizeof(content)) bufSize = sizeof(content);
    
    memcpy(buf, content, bufSize);
    return bufSize;
}

void cb_newFile(const char *filename, const char filenameSuffix[3], uint32_t fileSize){

  printf("",filename,filenameSuffix);
}


int main(int argc, const char * argv[]) {
    info("%s",VERSION_STRING);
    
    tihmstar::EmuFATFS fs;
    
    fs.addFile("hello world.txt", NULL, 0x1000000, info_read_cb);
    fs.registerNewfileCallback(cb_newFile);
    {
        uint8_t buf[0x2000];
        FILE *f=fopen("/Users/tihmstar/Desktop/ptr.bin","rb");
        fread(buf, 1, sizeof(buf), f);
        fclose(f);
        fs.catchRootDirectoryAccess(0,buf, sizeof(buf));
    }
    
    
    FILE *f = fopen("disk.bin", "wb");
    uint64_t totalRead = 0;
    uint64_t maxReadSize = 0x600000;
    while (totalRead < maxReadSize) {
        uint8_t buf[0x200];
        int32_t didread = 0;
        uint32_t needsRead = sizeof(buf);
        if (needsRead > maxReadSize - totalRead) needsRead = (uint32_t)(maxReadSize - totalRead);
        didread = fs.hostRead((uint32_t)totalRead, buf, needsRead);
        if (didread <= 0 ) break;
        fwrite(buf, 1, didread, f);
        totalRead += didread;
    }
    fclose(f);
    
    info("wrote 0x%llx bytes",totalRead);
    
    
    info("Done");
    return 0;
}
