#include "asdfs.h"
#include "asdfs_internal.h"

#define max(a,b) ((a)>(b)?(a):(b))

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn) {
    fprintf(stderr, "asdfs_init\n");

    init_inode();
    return NULL;
}

// 파일 시스템 정보 조회
int asdfs_statfs (const char *path, struct statvfs *buf) {
    fprintf(stderr, "asdfs_statfs\n");

    *buf = get_superblock();
    return 0;
}

// 파일 정보 조회
int asdfs_getattr (const char *path, struct stat *buf) {
    fprintf(stderr, "asdfs_getattr %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;

        case HEAD_NO_PERMISSION:
            return -EACCES;

        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    *buf = res.exact->attr;
    return 0;
}

// 디렉터리 생성
int asdfs_mkdir (const char *path, mode_t mode) {
    fprintf(stderr, "asdfs_mkdir %s %X\n", path, mode);

    search_result res;
    asdfs_errno code = find_inode(path, &res);

    struct fuse_context *context = fuse_get_context();
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    struct stat attr;
    memset(&attr, 0, sizeof(struct stat));
    attr.st_mode = S_IFDIR | mode;
    attr.st_nlink = 1;
    attr.st_uid = context->uid;
    attr.st_gid = context->gid;
    attr.st_atime = now.tv_sec; // 파일 최근 사용 시간
    attr.st_mtime = now.tv_sec; // 파일 최근 수정 시간
    attr.st_ctime = now.tv_sec; // 파일 최근 상태 변화 시간

    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            return -EEXIST;
            
        case EXACT_NOT_FOUND:
            break;
            
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    inode *node = NULL;
    code = create_inode(path, attr, &node);
    switch (code & 0xFFFF) {
        case NO_ERROR:
            break;

        case NO_FREE_SPACE:
            return -ENOSPC;

        default:
            return -EIO;
    }

    insert_inode(res.parent, res.left, res.right, node);
    return 0;
}

// 디렉터리 삭제
int asdfs_rmdir (const char *path) {
    fprintf(stderr, "asdfs_rmdir %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    if (res.exact->firstChild) {
        return -ENOTEMPTY;
    }

    destroy_inode(res.exact);
    return 0;
}

// 디렉터리 열기
int asdfs_opendir (const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_opendir %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;
    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(exact->attr.st_mode & S_IFDIR)) {
        return -ENOTDIR;
    }

    if (!(code & CAN_READ_EXACT)) {
        return -EACCES;
    }
    
    fi->fh = (uint64_t)exact;
    return 0;
}

// 디렉터리 읽기
int asdfs_readdir (const char *path, void *buf, fuse_fill_dir_t filer, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_readdir %s\n", path);

    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }

    filer(buf, ".", NULL, 0);
    filer(buf, "..", NULL, 0);
    
    inode *child = node->firstChild;
    while (child) {
        filer(buf, child->name, NULL, 0);
        child = child->rightSibling;
    }
    
    return 0;
}

// 파일 생성
int asdfs_mknod (const char *path, mode_t mode, dev_t rdev) {
    fprintf(stderr, "asdfs_mknod %s %X\n", path, mode);

    if (!(mode & S_IFREG)) {
        return -ENOSYS;
    }
    
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct fuse_context *context = fuse_get_context();

    struct stat attr;
    memset(&attr, 0, sizeof(struct stat));
    attr.st_mode = mode;
    attr.st_nlink = 1;
    attr.st_uid = context->uid;
    attr.st_gid = context->gid;
    attr.st_rdev = rdev;
    attr.st_atime = now.tv_sec; // 파일 최근 사용 시간
    attr.st_mtime = now.tv_sec; // 파일 최근 수정 시간
    attr.st_ctime = now.tv_sec; // 파일 최근 상태 변화 시간

    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            return -EEXIST;
            
        case EXACT_NOT_FOUND:
            break;
            
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    inode *node = NULL;
    code = create_inode(path, attr, &node);
    switch (code & 0xFFFF) {
        case NO_ERROR:
            break;

        case NO_FREE_SPACE:
            return -ENOSPC;

        default:
            return -EIO;
    }
    
    code = alloc_data_inode(node, 0);
    switch (code & 0xFFFF) {
        case NO_ERROR:
            break;

        case NO_FREE_SPACE:
            destroy_inode(node);
            return -ENOSPC;

        default:
            destroy_inode(node);
            return -EIO;
    }
    
    insert_inode(res.parent, res.left, res.right, node);
    return 0;
}

// 파일 삭제
int asdfs_unlink (const char *path) {
    fprintf(stderr, "asdfs_unlink %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    destroy_inode(res.exact);
    return 0;
}

// 파일 열기
int asdfs_open (const char *path, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_open %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;
    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (exact->attr.st_mode & S_IFDIR) {
        return -EISDIR;
    }

    int flags = fi->flags;
    if (!(
            (((flags & 3) == O_RDONLY) && (code & CAN_READ_EXACT))
            || (((flags & 5) & O_WRONLY) && (code & CAN_WRITE_EXACT))
            || (((flags & 1) & O_RDWR) && (code & CAN_READ_EXACT) && (code & CAN_WRITE_EXACT))
        )) {
        return -EACCES;
    }

    fi->fh = (uint64_t)exact;
    return 0;
}

// 생성 및 수정 시간 변경
int asdfs_utimens (const char *path, const struct timespec tv[2]) {
    fprintf(stderr, "asdfs_utimens %s\n", path);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;

    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            exact->attr.st_atime = tv[0].tv_sec;
            exact->attr.st_mtime = tv[1].tv_sec;
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 파일 읽기
int asdfs_read (const char *path, char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_read %s %zu %zu\n", path, size, off);

    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }

    if (node->attr.st_mode & S_IFDIR) {
        return -EISDIR;
    }

    void *data = node->data;
    if (data == NULL) {
        return -EIO;
    }

    char *ptr = (char *)data + off;
    for (size_t i=0; i<size; i++) {
        mem[i] = ptr[i];
    }

    return (int)size;
}

// 이미 있는 파일 크기 변경
int asdfs_truncate (const char *path, off_t size) {
    fprintf(stderr, "asdfs_truncate %s %zu\n", path, size);

    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case HEAD_NO_PERMISSION:
            return -EACCES;

        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & CAN_WRITE_EXACT)) {
        return -EACCES;
    }

    code = alloc_data_inode(res.exact, size);
    switch (code & 0xFFFF) {
        case NO_ERROR:
            break;

        case NO_FREE_SPACE:
            return -ENOSPC;

        default:
            return -EIO;
    }

    return 0;
}

// 파일 쓰기
int asdfs_write (const char *path, const char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    fprintf(stderr, "asdfs_write %s %zu %zu\n", path, size, off);

    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }
    
    asdfs_errno code = alloc_data_inode(node, max(node->attr.st_size, off + size));
    switch (code & 0xFFFF) {
        case NO_ERROR:
            break;

        case NO_FREE_SPACE:
            return -ENOSPC;

        default:
            return -EIO;
    }

    char *ptr = (char *)(node->data) + off;
    for (size_t i=0; i<size; i++) {
        ptr[i] = mem[i];
    }

    return (int)size;
}

// 파일 권한 변경
int asdfs_chmod (const char *path, mode_t mode) {
    fprintf(stderr, "asdfs_chmod %s %X\n", path, mode);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;

        case HEAD_NO_PERMISSION:
            return -EACCES;

        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(code & IS_OWNER)) {
        return -EPERM;
    }

    res.exact->attr.st_mode = mode;
    return 0;
}

// 파일 소유자 변경
int asdfs_chown (const char *path, uid_t uid, gid_t gid) {
    fprintf(stderr, "asdfs_chown %s %u %u\n", path, uid, gid);

    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    switch (code & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;

        case HEAD_NO_PERMISSION:
            return -EACCES;

        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    struct fuse_context *context = fuse_get_context();
    if (context->uid != 0) {
        return -EPERM;
    }

    res.exact->attr.st_uid = uid;
    res.exact->attr.st_gid = gid;
    return 0;
}

// 파일 이동
int asdfs_rename (const char *oldpath, const char *newpath) {
    fprintf(stderr, "asdfs_rename %s %s\n", oldpath, newpath);

    search_result oldres;
    asdfs_errno oldcode = find_inode(oldpath, &oldres);
    
    switch (oldcode & 0xFFFF) {
        case EXACT_FOUND:
            break;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;

        case HEAD_NO_PERMISSION:
            return -EACCES;

        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(oldcode & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    search_result newres;
    asdfs_errno newcode = find_inode(newpath, &newres);
    
    switch (newcode & 0xFFFF) {
        case EXACT_FOUND:
            return -EEXIST;
            
        case EXACT_NOT_FOUND:
            break;

        case HEAD_NOT_FOUND:
            return -ENOENT;

        case HEAD_NO_PERMISSION:
            return -EACCES;

        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }

    if (!(newcode & CAN_WRITE_PARENT)) {
        return -EACCES;
    }

    extract_inode(oldres.exact);
    insert_inode(newres.parent, newres.left, newres.right, oldres.exact);
    return 0;
}
