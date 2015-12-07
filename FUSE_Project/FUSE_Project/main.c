#include "asdfs.h"
#include <fuse.h>

static struct fuse_operations asdfs_oper = {
    .init     = asdfs_init,     // 파일 시스템 초기화
    .statfs   = asdfs_statfs,   // 파일 시스템 정보 조회
    .getattr  = asdfs_getattr,  // 파일 정보 조회

    .mkdir    = asdfs_mkdir,    // 디렉터리 생성
    .rmdir    = asdfs_rmdir,    // 디렉터리 삭제
    .opendir  = asdfs_opendir,  // 디렉터리 열기
    .readdir  = asdfs_readdir,  // 디렉터리 읽기

    .mknod    = asdfs_mknod,    // 파일 생성
    .utimens  = asdfs_utimens,  // 생성 및 수정 시간 변경
    .unlink   = asdfs_unlink,   // 파일 삭제

    .open     = asdfs_open,     // 파일 열기
    .read     = asdfs_read,     // 파일 읽기
    .truncate = asdfs_truncate, // 이미 있는 파일 크기 변경
    .write    = asdfs_write,    // 파일 쓰기

    .chmod    = asdfs_chmod,    // 파일 권한 변경
    .chown    = asdfs_chown,    // 파일 소유자 변경
    .rename   = asdfs_rename,   // 파일 이동
};

int main(int argc, char *argv[]) {
    // fuse 파일 시스템 시작
    return fuse_main(argc, argv, &asdfs_oper, NULL);
}
