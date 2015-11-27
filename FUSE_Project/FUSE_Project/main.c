#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define FUSE_USE_VERSION 29
#include "fuse.h" // DEBUG

#define BLOCK_SIZE_KB   4     // 블록 크기 (KB)
#define VOLUME_SIZE_MB  100   // 파일 시스템 볼륨 크기 (MB)
#define MAX_FILENAME    255   // 최대 파일 이름 길이
#define INODE_SIZE_BYTE 512   // 각 inode당 메모리 크기

typedef struct inode inode;
struct inode {
    struct stat attr;
    
    uint8_t name[MAX_FILENAME + 1];
    
    inode *parent;
    inode *leftSibling;
    inode *rightSibling;
    inode *firstChild;
    inode *lastChild;
    
    void *data;
};

static inode root;



int main(int argc, const char * argv[]) {
    return 1;
}
