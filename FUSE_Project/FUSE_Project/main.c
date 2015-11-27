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

static struct statvfs superblock;
static inode root;

// 파일 시스템 초기화
void *os_init (struct fuse_conn_info *conn) {
    time_t current_time = time(NULL);
    struct fuse_context *context = fuse_get_context();
    
    // root inode 초기화
    root.attr.st_mode  = S_IFDIR;      // 파일 모드
    root.attr.st_nlink = 1;            // 파일 링크 개수
    root.attr.st_uid   = context->uid; // 파일 사용자 ID
    root.attr.st_gid   = context->gid; // 파일 그룹 ID
    root.attr.st_atime = current_time; // 파일 최근 사용 시간
    root.attr.st_mtime = current_time; // 파일 최근 수정 시간
    root.attr.st_ctime = current_time; // 파일 최근 상태 변화 시간
    // 나머지 값은 static이므로 전부 0.
    
    // superblock 초기화
    
    // f_bsize: 파일 시스템 블록 크기
    unsigned long f_bsize = BLOCK_SIZE_KB * 1024;
    
    // f_blocks: 파일 시스템 내 전체 블록 개수
    fsblkcnt_t f_blocks = VOLUME_SIZE_MB * 1024 / BLOCK_SIZE_KB;
    
    // f_files: 전체 파일 시리얼 넘버 (inode) 개수
    fsfilcnt_t f_files = (fsfilcnt_t)(f_blocks * (f_bsize / INODE_SIZE_BYTE));
    
    superblock.f_bsize   = f_bsize;      // 파일 시스템 블록 크기
    superblock.f_blocks  = f_blocks;     // 파일 시스템 내 전체 블록 개수
    superblock.f_bfree   = f_blocks - 1; // 사용 가능한 블록 수, root만큼 제외
    superblock.f_bavail  = f_blocks - 1; // 일반 권한 프로세스가 사용 가능한 블록 수, root만큼 제외
    superblock.f_files   = f_files;      // 전체 파일 시리얼 넘버 개수
    superblock.f_favail  = f_files - 1;  // 사용 가능한 파일 시리얼 넘버 개수, root만큼 제외
    superblock.f_namemax = MAX_FILENAME; // 최대 파일 이름 길이
    // 나머지 값은 static이므로 전부 0.
    
    return NULL;
}

static struct fuse_operations os_oper = {
    .init       = os_init
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &os_oper, NULL);
}

