#ifndef __ASDFS_INTERNAL_H__
#define __ASDFS_INTERNAL_H__

#define BLOCK_SIZE_KB   4     // 블록 크기 (KB)
#define VOLUME_SIZE_MB  100   // 파일 시스템 볼륨 크기 (MB)
#define MAX_FILENAME    255   // 최대 파일 이름 길이 (B)
#define INODE_SIZE_BYTE 512   // 각 inode당 메모리 크기 (B)

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

// inode 구조체
typedef struct inode inode;
struct inode {
    struct stat attr;    // 파일의 상태 정보 메타데이터
    
    char name[MAX_FILENAME + 1]; // 파일 이름
    
    inode *parent;       // 상위 inode 객체 포인터
    inode *leftSibling;  // 왼쪽 inode 객체 포인터
    inode *rightSibling; // 오른쪽 inode 객체 포인터
    inode *firstChild;   // 하위 첫번째 inode 객체 포인터
    inode *lastChild;    // 하위 마지막 inode 객체 포인터
                         // fistChild에서 lastChild까지는 ABC순으로 유지
    
    void *data;          // 실제 파일 데이터
};

// find_inode에서 반환되는 inode 검색 결과
typedef struct search_result search_result;
struct search_result {
    inode *parent; // 상위 inode 객체 포인터

    inode *left;   // exact의 왼쪽 inode 객체 포인터
    inode *exact;  // 요청된 path에 해당하는 inode 객체 포인터
    inode *right;  // exact의 오른쪽 inode 객체 포인터
};

// asdFS 에러 코드
typedef enum {
    // LSB 2바이트: 주요 오류 번호
    NO_ERROR = 0,       // 오류 없음
    
    EXACT_FOUND,        // 주어진 위치에 inode 있음
                        // exact/left/right/parent는 NULL이 아님
                        // 해당 위치의 inode에 대한 위치 정보 반환

    EXACT_NOT_FOUND,    // 주어진 위치에 inode 없음
                        // exact는 NULL이나 parent/left/right는 NULL이 아님
                        // 새로운 inode가 추가될 경우의 위치 정보 반환
    
    HEAD_NOT_FOUND,     // 주어진 위치로의 tree path 없음
    HEAD_NOT_DIRECTORY, // tree path 중간에 디렉터리가 아닌 inode 있음
    HEAD_NO_PERMISSION, // tree path 탐색 권한 없음

    NO_FREE_SPACE,          // 파일 시스템에 남은 용량 없음
    GENERAL_ERROR = 0xFFFF, // 그 외 에러

    // MSB 2바이트: context 참조 보조 비트 마스크
    IS_OWNER = 1 << 22,           // 호출 프로세스가 exact의 소유자인지 여부
    CAN_READ_PARENT = 1 << 21,    // 호출 프로세스의 parent 읽기 권한 여부
    CAN_WRITE_PARENT = 1 << 20,   // 호출 프로세스의 parent 쓰기 권한 여부
    CAN_EXECUTE_PARENT = 1 << 19, // 호출 프로세스의 parent 탐색 권한 여부
    CAN_READ_EXACT = 1 << 18,     // 호출 프로세스의 exact 읽기 권한 여부
    CAN_WRITE_EXACT = 1 << 17,    // 호출 프로세스의 exact 쓰기 권한 여부
    CAN_EXECUTE_EXACT = 1 << 16   // 호출 프로세스의 exact 실행/탐색 권한 여부
} asdfs_errno;

// 파일 시스템 root inode, superblock 초기화
void init_root_superblock();

// 파일 시스템 superblock 정보 반환
struct statvfs get_superblock();

// path에 해당하는 inode 검색, 결과 res 포인터로 반환
asdfs_errno find_inode(const char *path, search_result *res);

// 새로운 inode 생성, res 포인터로 반환
asdfs_errno create_inode(const char *path, struct stat attr, inode **out);

// node에 data 공간 할당
asdfs_errno alloc_data_inode(inode *node, off_t size);

// node의 data 공간 반환
void dealloc_data_inode(inode *node);

// 새로운 inode를 res 위치에 삽입
void insert_inode(search_result res, inode *new);

// node를 inode tree에서 분리
void extract_inode(inode *node);

// inode 삭제
void destroy_inode(inode *node);

#endif
