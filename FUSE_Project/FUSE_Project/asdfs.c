#include "asdfs.h"
#include "asdfs_internal.h"

// max: a, b 중 최댓값 반환하는 매크로
#define max(a,b) ((a)>(b)?(a):(b))

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn) {
    fprintf(stderr, "asdfs_init\n");

    // 내부 root/superblock 초기화 함수 호출
    init_root_superblock();

    return NULL;
}

// 파일 시스템 정보 조회
int asdfs_statfs (const char *path, struct statvfs *buf) {
    fprintf(stderr, "asdfs_statfs\n");

    // 내부 superblock 메타데이터 반환
    *buf = get_superblock();

    return 0;
}

// 파일 정보 조회
int asdfs_getattr (const char *path, struct stat *buf) {
    fprintf(stderr, "asdfs_getattr %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory

        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // exact의 attr 구조체 반환
    *buf = res.exact->attr;
    return 0;
}

// 디렉터리 생성
int asdfs_mkdir (const char *path, mode_t mode) {
    fprintf(stderr, "asdfs_mkdir %s %X\n", path, mode);

    // 현재 fuse context 가져오기. 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();

    // 현재 시간 가져오기
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // 파일 메타데이터 생성
    struct stat attr;
    memset(&attr, 0, sizeof(struct stat)); // 0으로 리셋
    attr.st_mode = S_IFDIR | mode; // 주어진 권한에 디렉터리 플래그 포함
    attr.st_nlink = 1;             // 최초 링크는 1개
    attr.st_uid = context->uid;    // 호출 프로세스의 uid를 소유자로 지정
    attr.st_gid = context->gid;    // 호출 프로세스의 gid를 그룹으로 지정
    attr.st_atime = now.tv_sec;    // 파일 최근 사용 시간
    attr.st_mtime = now.tv_sec;    // 파일 최근 수정 시간
    attr.st_ctime = now.tv_sec;    // 파일 최근 상태 변화 시간

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            return -EEXIST;      // File exists
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
            break;               // 계속 진행 ->
            
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_WRITE_PARENT)) {   // parent에 쓰기 권한이 없는 경우
        return -EACCES;                 // Permission denied
    }

    // 새로운 inode 생성
    inode *node = NULL;
    code = create_inode(path, attr, &node);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case NO_ERROR:           // 오류 없음
            break;               // 계속 진행 ->

        case NO_FREE_SPACE:      // 남은 용량 없음
            return -ENOSPC;      // No space left on device

        default:                 // 그 외
            return -EIO;         // Input/output error
    }

    // 새로운 inode를 res 위치에 삽입
    insert_inode(res, node);
    return 0;
}

// 디렉터리 삭제
int asdfs_rmdir (const char *path) {
    fprintf(stderr, "asdfs_rmdir %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_WRITE_PARENT)) {   // parent에 쓰기 권한이 없는 경우
        return -EACCES;                 // Permission denied
    }

    // inode 상태 검사
    inode *exact = res.exact;

    if (!(exact->attr.st_mode & S_IFDIR)) { // exact가 디렉터리가 아닌 경우
        return -ENOTDIR;                    // Not a directory
    }

    if (exact->firstChild) {            // exact에 자식 inode가 있을 경우
        return -ENOTEMPTY;              // Directory not empty
    }

    // inode 삭제
    destroy_inode(exact);
    return 0;
}

// 디렉터리 열기
int asdfs_opendir (const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_opendir %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // inode 상태 검사
    inode *exact = res.exact;

    if (!(exact->attr.st_mode & S_IFDIR)) { // exact가 디렉터리가 아닌 경우
        return -ENOTDIR;                    // Not a directory
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_READ_EXACT)) { // exact에 읽기 권한이 없는 경우
        return -EACCES;             // Permission denied
    }
    
    // (fuse_file_info*)fi의 fh(file handle)로 포인터 전달
    fi->fh = (uint64_t)exact;
    return 0;
}

// 디렉터리 읽기
int asdfs_readdir (const char *path, void *buf, fuse_fill_dir_t filer, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_readdir %s\n", path);

    // asdfs_opendir에서 전달된 file handle 확인
    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }

    // ".", ".."
    filer(buf, ".", NULL, 0);
    filer(buf, "..", NULL, 0);
    
    // node의 firstChild부터 rightSibling을 child로 따라가며 반복
    inode *child = node->firstChild;
    while (child) {
        // child name 전달
        filer(buf, child->name, NULL, 0);
        child = child->rightSibling;
    }
    
    return 0;
}

// 파일 생성
int asdfs_mknod (const char *path, mode_t mode, dev_t rdev) {
    fprintf(stderr, "asdfs_mknod %s %X\n", path, mode);

    // 요청 상태 검사
    if (!(mode & S_IFREG)) { // 요청된 파일 mode가 일반 파일이 아닌 경우
        return -ENOSYS;      // Function not implemented
    }
    
    // 현재 fuse context 가져오기. 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();

    // 현재 시간 가져오기
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    // 파일 메타데이터 생성
    struct stat attr;
    memset(&attr, 0, sizeof(struct stat)); // 0으로 리셋
    attr.st_mode = mode;        // 주어진 권한 지정
    attr.st_nlink = 1;          // 최초 링크는 1개
    attr.st_uid = context->uid; // 호출 프로세스의 uid를 소유자로 지정
    attr.st_gid = context->gid; // 호출 프로세스의 gid를 그룹으로 지정
    attr.st_rdev = rdev;        // 지정된 기기 ID 지정
    attr.st_atime = now.tv_sec; // 파일 최근 사용 시간
    attr.st_mtime = now.tv_sec; // 파일 최근 수정 시간
    attr.st_ctime = now.tv_sec; // 파일 최근 상태 변화 시간

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            return -EEXIST;      // File exists
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
            break;               // 계속 진행 ->
            
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_WRITE_PARENT)) {   // parent에 쓰기 권한이 없는 경우
        return -EACCES;                 // Permission denied
    }

    // 새로운 inode 생성
    inode *node = NULL;
    code = create_inode(path, attr, &node);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case NO_ERROR:           // 오류 없음
            break;               // 계속 진행 ->

        case NO_FREE_SPACE:      // 남은 용량 없음
            return -ENOSPC;      // No space left on device

        default:                 // 그 외
            return -EIO;         // Input/output error
    }
    
    // node에 data 공간 할당
    code = alloc_data_inode(node, 0);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case NO_ERROR:           // 오류 없음
            break;               // 계속 진행 ->

        case NO_FREE_SPACE:      // 남은 용량 없음
            destroy_inode(node); // inode 삭제
            return -ENOSPC;      // No space left on device

        default:                 // 그 외
            destroy_inode(node); // inode 삭제
            return -EIO;         // Input/output error
    }
    
    // 새로운 inode를 res 위치에 삽입
    insert_inode(res, node);
    return 0;
}

// 생성 및 수정 시간 변경
int asdfs_utimens (const char *path, const struct timespec tv[2]) {
    fprintf(stderr, "asdfs_utimens %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // inode에 주어진 시간 값 저장
    inode *exact = res.exact;
    exact->attr.st_atime = tv[0].tv_sec; // 파일 최근 사용 시간
    exact->attr.st_mtime = tv[1].tv_sec; // 파일 최근 수정 시간
    return 0;
}

// 파일 삭제
int asdfs_unlink (const char *path) {
    fprintf(stderr, "asdfs_unlink %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_WRITE_PARENT)) {   // parent에 쓰기 권한이 없는 경우
        return -EACCES;                 // Permission denied
    }

    // inode 삭제
    destroy_inode(res.exact);
    return 0;
}

// 파일 열기
int asdfs_open (const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_open %s\n", path);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // inode 상태 검사
    inode *exact = res.exact;

    if (exact->attr.st_mode & S_IFDIR) { // exact가 디렉터리인 경우
        return -EISDIR;                  // Is a directory
    }

    // open 요청 파일 상태 flag 확인
    int flags = fi->flags;
    if (!(
            // 읽기 전용 요청이며 exact에 읽기 권한이 있거나
            (((flags & 3) == O_RDONLY) && (code & CAN_READ_EXACT))
            // 쓰기 전용 요청이며 exact에 쓰기 권한이 있거나
            || (((flags & 5) & O_WRONLY) && (code & CAN_WRITE_EXACT))
            // 읽기 및 쓰기 요청이며 exact에 읽기 권한과 쓰기 권한이 있는 경우가
            || (((flags & 1) & O_RDWR) && (code & CAN_READ_EXACT) && (code & CAN_WRITE_EXACT))
        )) { // 아닌 경우,
        return -EACCES; // Permission denied
    }

    // (fuse_file_info*)fi의 fh(file handle)로 포인터 전달
    fi->fh = (uint64_t)exact;
    return 0;
}

// 파일 읽기
int asdfs_read (const char *path, char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_read %s %zu %zu\n", path, size, off);

    // asdfs_open에서 전달된 file handle 확인
    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }

    if (node->attr.st_mode & S_IFDIR) { // node가 디렉터리인 경우
        return -EISDIR;                 // Is a directory
    }

    void *data = node->data;
    if (data == NULL) { // 할당된 data가 없을 경우
        return -EIO;    // Input/output error
    }

    // data의 offset부터 (offset + size)까지 mem으로 복사
    char *ptr = (char *)data + off;
    for (size_t i=0; i<size; i++) {
        mem[i] = ptr[i];
    }

    // 읽은 바이트 수 반환
    return (int)size;
}

// 이미 있는 파일 크기 변경
int asdfs_truncate (const char *path, off_t size) {
    fprintf(stderr, "asdfs_truncate %s %zu\n", path, size);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & CAN_WRITE_EXACT)) { // exact에 쓰기 권한이 없는 경우
        return -EACCES;              // Permission denied
    }

    // node에 data 공간 할당
    code = alloc_data_inode(res.exact, size);

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case NO_ERROR:           // 오류 없음
            return 0;            // 완료

        case NO_FREE_SPACE:      // 남은 용량 없음
            return -ENOSPC;      // No space left on device

        default:                 // 그 외
            return -EIO;         // Input/output error
    }
}

// 파일 쓰기
int asdfs_write (const char *path, const char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_write %s %zu %zu\n", path, size, off);

    // asdfs_open에서 전달된 file handle 확인
    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }
    
    // node에 data 공간 할당
    // 쓰기에서 요청한 (off + size)와 파일 크기 중 큰 것 선택
    asdfs_errno code = alloc_data_inode(node, max(node->attr.st_size, off + size));

    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case NO_ERROR:           // 오류 없음
            break;               // 계속 진행 ->

        case NO_FREE_SPACE:      // 남은 용량 없음
            return -ENOSPC;      // No space left on device

        default:                 // 그 외
            return -EIO;         // Input/output error
    }

    // data의 offset부터 (offset + size)까지 data로 복사
    char *ptr = (char *)(node->data) + off;
    for (size_t i=0; i<size; i++) {
        ptr[i] = mem[i];
    }

    // 쓴 바이트 수 반환
    return (int)size;
}

// 파일 권한 변경
int asdfs_chmod (const char *path, mode_t mode) {
    fprintf(stderr, "asdfs_chmod %s %X\n", path, mode);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory

        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // code 보조 비트 마스크 검사
    if (!(code & IS_OWNER)) { // 호출 프로세스가 owner가 아니면
        return -EPERM;        // Operation not permitted
    }

    // exact의 권한 정보 변경
    res.exact->attr.st_mode = mode;
    return 0;
}

// 파일 소유자 변경
int asdfs_chown (const char *path, uid_t uid, gid_t gid) {
    fprintf(stderr, "asdfs_chown %s %u %u\n", path, uid, gid);

    // path에 해당하는 inode 검색
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    // code 주요 오류 번호 검사
    switch (code & 0xFFFF) {
        case EXACT_FOUND:        // path 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // path 위치에 inode 없음
        case HEAD_NOT_FOUND:     // path의 head 없음
            return -ENOENT;      // No such file or directory

        case HEAD_NOT_DIRECTORY: // path의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // path의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }


    // 현재 fuse context 가져오기. 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();
    if (context->uid != 0) { // 호출 프로세스가 superuser가 아닌 경우
        return -EPERM;       // Operation not permitted
    }

    // exact의 소유자 정보 변경
    res.exact->attr.st_uid = uid;
    res.exact->attr.st_gid = gid;
    return 0;
}

// 파일 이동
int asdfs_rename (const char *oldpath, const char *newpath) {
    fprintf(stderr, "asdfs_rename %s %s\n", oldpath, newpath);

    // oldpath에 해당하는 inode 검색
    search_result oldres;
    asdfs_errno oldcode = find_inode(oldpath, &oldres);
    
    // oldcode 주요 오류 번호 검사
    switch (oldcode & 0xFFFF) {
        case EXACT_FOUND:        // oldpath 위치에 inode 있음
            break;               // 계속 진행 ->
            
        case EXACT_NOT_FOUND:    // oldpath 위치에 inode 없음
        case HEAD_NOT_FOUND:     // oldpath의 head 없음
            return -ENOENT;      // No such file or directory

        case HEAD_NOT_DIRECTORY: // oldpath의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // oldpath의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // oldcode 보조 비트 마스크 검사
    if (!(oldcode & CAN_WRITE_PARENT)) { // oldpath의 parent에 쓰기 권한이 없는 경우
        return -EACCES;                  // Permission denied
    }

    // newpath에 해당하는 inode 검색
    search_result newres;
    asdfs_errno newcode = find_inode(newpath, &newres);
    
    // newcode 주요 오류 번호 검사
    switch (newcode & 0xFFFF) {
        case EXACT_FOUND:        // newpath 위치에 inode 있음
            return -EEXIST;      // File exists
            
        case EXACT_NOT_FOUND:    // newpath 위치에 inode 없음
            break;               // 계속 진행 ->
            
        case HEAD_NOT_FOUND:     // newpath의 head 없음
            return -ENOENT;      // No such file or directory
            
        case HEAD_NOT_DIRECTORY: // newpath의 head가 디렉터리가 아님
            return -ENOTDIR;     // Not a directory
            
        case HEAD_NO_PERMISSION: // newpath의 head를 탐색할 권한이 없음
            return -EACCES;      // Permission denied

        case GENERAL_ERROR:      // 그 외
        default:
            return -EIO;         // Input/output error
    }

    // newcode 보조 비트 마스크 검사
    if (!(newcode & CAN_WRITE_PARENT)) { // newpath의 parent에 쓰기 권한이 없는 경우
        return -EACCES;                  // Permission denied
    }

    // oldres.exact를 inode tree에서 분리
    extract_inode(oldres.exact);

    // oldres.exact를 newres 위치에 삽입
    insert_inode(newres, oldres.exact);
    return 0;
}
