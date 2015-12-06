#ifndef __ASDFS_H__
#define __ASDFS_H__

#define FUSE_USE_VERSION 29   // 사용할 FUSE API 버전

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn);

// 파일 시스템 정보 조회
int asdfs_statfs (const char *path, struct statvfs *buf);

// 파일 정보 조회
int asdfs_getattr (const char *path, struct stat *buf);

// 디렉터리 생성
int asdfs_mkdir (const char *path, mode_t mode);

// 디렉터리 삭제
int asdfs_rmdir (const char *path);

// 디렉터리 열기
int asdfs_opendir (const char *path, struct fuse_file_info *fi);

// 디렉터리 읽기
int asdfs_readdir (const char *path, void *buf, fuse_fill_dir_t filer, off_t off, struct fuse_file_info *fi);

// 파일 생성
int asdfs_mknod (const char *path, mode_t mode, dev_t rdev);

// 파일 삭제
int asdfs_unlink (const char *path);

// 파일 열기
int asdfs_open (const char *path, struct fuse_file_info *fi);

// 생성 및 수정 시간 변경
int asdfs_utimens (const char *path, const struct timespec tv[2]);

// 파일 읽기
int asdfs_read (const char *path, char *mem, size_t size, off_t off, struct fuse_file_info *fi);

// 이미 있는 파일 크기 변경
int asdfs_truncate (const char *path, off_t size);

// 파일 쓰기
int asdfs_write (const char *path, const char *mem, size_t size, off_t off, struct fuse_file_info *fi);

// 파일 권한 변경
int asdfs_chmod (const char *path, mode_t mode);

// 파일 소유자 변경
int asdfs_chown (const char *path, uid_t uid, gid_t gid);

// 파일 이동
int asdfs_rename (const char *oldpath, const char *newpath);

static struct fuse_operations asdfs_oper = {
    .init     = asdfs_init,
    .statfs   = asdfs_statfs,
    .getattr  = asdfs_getattr,

    .mkdir    = asdfs_mkdir,
    .rmdir    = asdfs_rmdir,
    .opendir  = asdfs_opendir,
    .readdir  = asdfs_readdir,

    .mknod    = asdfs_mknod,
    .unlink   = asdfs_unlink,
    .open     = asdfs_open,
    .utimens  = asdfs_utimens,
    .read     = asdfs_read,
    .truncate = asdfs_truncate,
    .write    = asdfs_write,

    .chmod    = asdfs_chmod,
    .chown    = asdfs_chown,   
    .rename   = asdfs_rename, 
};

#endif