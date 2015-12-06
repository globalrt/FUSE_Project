#ifndef __ASDFS_INTERNAL_H__
#define __ASDFS_INTERNAL_H__

#define BLOCK_SIZE_KB   4     // 블록 크기 (KB)
#define VOLUME_SIZE_MB  100   // 파일 시스템 볼륨 크기 (MB)
#define MAX_FILENAME    255   // 최대 파일 이름 길이
#define INODE_SIZE_BYTE 512   // 각 inode당 메모리 크기

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

typedef struct inode inode;
struct inode {
    struct stat attr;
    
    char name[MAX_FILENAME + 1];
    
    inode *parent;
    inode *leftSibling;
    inode *rightSibling;
    inode *firstChild;
    inode *lastChild;
    
    void *data;
};

typedef struct search_result search_result;
struct search_result {
    inode *parent;

    inode *left;
    inode *exact;
    inode *right;
};

typedef enum {
    NO_ERROR = 0,
    
    EXACT_FOUND,
    EXACT_NOT_FOUND,
    HEAD_NOT_FOUND,
    HEAD_NOT_DIRECTORY,
    HEAD_NO_PERMISSION,

    NO_FREE_SPACE,
    GENERAL_ERROR = 0xFFFF,

    IS_OWNER = 1 << 22,
    CAN_READ_PARENT = 1 << 21,
    CAN_WRITE_PARENT = 1 << 20,
    CAN_EXECUTE_PARENT = 1 << 19,
    CAN_READ_EXACT = 1 << 18,
    CAN_WRITE_EXACT = 1 << 17,
    CAN_EXECUTE_EXACT = 1 << 16
} asdfs_errno;

void init_inode();

struct statvfs get_superblock();

asdfs_errno find_inode(const char *path, search_result *res);

asdfs_errno create_inode(const char *path, struct stat attr, inode **out);

asdfs_errno alloc_data_inode(inode *node, off_t size);

void dealloc_data_inode(inode *node);

void insert_inode(inode *parent, inode *left, inode *right, inode *new);

void extract_inode(inode *node);

void destroy_inode(inode *node);

#endif
