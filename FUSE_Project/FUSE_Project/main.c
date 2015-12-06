#define FUSE_USE_VERSION 29   // 사용할 FUSE API 버전
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

#define max(a,b) ((a)>(b)?(a):(b))

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

static struct statvfs superblock;
static inode root;

int is_root() {
    struct fuse_context *context = fuse_get_context();
    return context->uid == 0;
}

int can_read(inode *node) {
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    mode_t file_mode = node->attr.st_mode;
    uid_t file_uid = node->attr.st_uid;
    uid_t file_gid = node->attr.st_gid;

    // owner이고, 파일에 owner READ 권한이 있는 경우
    if (((curr_uid == file_uid) && (file_mode & S_IRUSR))
        // owner는 아니지만 group에 속하고, 파일에 group READ 권한이 있는 경우
        || ((curr_gid == file_gid) && (file_mode & S_IRGRP)) ) {
        return 1;
    }

    // supplementary groups 확인
    gid_t list[512];
    int length = fuse_getgroups(512, list);
    for (int i=0; i<length && i<512; i++) {
        uid_t supp_gid = list[i];

        // owner는 아니지만 supplementary group에 속하고,
        // 파일에 group READ 권한이 있는 경우
        if ((supp_gid == file_gid) && (file_mode & S_IRGRP)) {
            return 1;
        }
    }

    // others이고, 파일에 others READ 권한이 있는 경우
    if (file_mode & S_IROTH) {
        return 1;
    }

    return 0;
}

int can_write(inode *node) {
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    mode_t file_mode = node->attr.st_mode;
    uid_t file_uid = node->attr.st_uid;
    uid_t file_gid = node->attr.st_gid;

    // owner이고, 파일에 owner WRITE 권한이 있는 경우
    if (((curr_uid == file_uid) && (file_mode & S_IWUSR))
        // owner는 아니지만 group에 속하고, 파일에 group WRITE 권한이 있는 경우
        || ((curr_gid == file_gid) && (file_mode & S_IWGRP)) ) {
        return 1;
    }

    // supplementary groups 확인
    gid_t list[512];
    int length = fuse_getgroups(512, list);
    for (int i=0; i<length && i<512; i++) {
        uid_t supp_gid = list[i];

        // owner는 아니지만 supplementary group에 속하고,
        // 파일에 group WRITE 권한이 있는 경우
        if ((supp_gid == file_gid) && (file_mode & S_IWGRP)) {
            return 1;
        }
    }

    // others이고, 파일에 others WRITE 권한이 있는 경우
    if (file_mode & S_IWOTH) {
        return 1;
    }

    return 0;
}

int can_execute(inode *node) {
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    mode_t file_mode = node->attr.st_mode;
    uid_t file_uid = node->attr.st_uid;
    uid_t file_gid = node->attr.st_gid;

    // owner이고, 파일에 owner EXECUTE 권한이 있는 경우
    if (((curr_uid == file_uid) && (file_mode & S_IXUSR))
        // owner는 아니지만 group에 속하고, 파일에 group EXECUTE 권한이 있는 경우
        || ((curr_gid == file_gid) && (file_mode & S_IXGRP)) ) {
        return 1;
    }

    // supplementary groups 확인
    gid_t list[512];
    int length = fuse_getgroups(512, list);
    for (int i=0; i<length && i<512; i++) {
        uid_t supp_gid = list[i];

        // owner는 아니지만 supplementary group에 속하고,
        // 파일에 group EXECUTE 권한이 있는 경우
        if ((supp_gid == file_gid) && (file_mode & S_IXGRP)) {
            return 1;
        }
    }

    // others이고, 파일에 others EXECUTE 권한이 있는 경우
    if (file_mode & S_IXOTH) {
        return 1;
    }

    return 0;
}

asdfs_errno child_search(inode *parent, const char *search_name, search_result *res){
    if (parent == NULL || search_name == NULL || res == NULL) {
        return GENERAL_ERROR;
    }
    
    res->parent = parent;
    
    if(parent->firstChild==NULL){
        res->left=NULL;
        res->exact=NULL;
        res->right=NULL;
        
        return EXACT_NOT_FOUND; // no child.
    }
    
    inode *firstChild = parent->firstChild;

    if (strcmp(search_name,firstChild->name)<0) {
        res->left = NULL;
        res->exact = NULL;
        res->right = firstChild;
        return EXACT_NOT_FOUND;
    }

    inode *lastChild = parent->lastChild;

    if (strcmp(search_name,lastChild->name)>0) {
        res->left = lastChild;
        res->exact = NULL;
        res->right = NULL;
        return EXACT_NOT_FOUND;
    }
    
    while (strcmp(search_name,firstChild->name)>0) {
        firstChild = firstChild->rightSibling;
    }
    
    if (strcmp(search_name,firstChild->name)==0) {
        res->left = firstChild->leftSibling;
        res->exact = firstChild;
        res->right = firstChild->rightSibling;
        return EXACT_FOUND;
    }
    else{
        res->left = firstChild->leftSibling;
        res->exact = NULL;
        res->right = firstChild;
        return EXACT_NOT_FOUND;
    }
}

void init_inode() {
    // 현재 시간 가져오기
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    
    struct fuse_context *context = fuse_get_context();

    // root inode 초기화
    strncpy(root.name, "ROOT", MAX_FILENAME);
    root.attr.st_mode  = S_IFDIR | (0777 & ~(context->umask)); // 파일 모드
    root.attr.st_nlink = 1;              // 파일 링크 개수
    root.attr.st_uid   = context->uid;   // 
    root.attr.st_gid   = context->gid;   //
    root.attr.st_atime = now.tv_sec;     // 파일 최근 사용 시간
    root.attr.st_mtime = now.tv_sec;     // 파일 최근 수정 시간
    root.attr.st_ctime = now.tv_sec;     // 파일 최근 상태 변화 시간
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
}

asdfs_errno find_inode(const char *path, search_result *res){
    res->parent = NULL;
    res->left = NULL;
    res->exact = NULL;
    res->right = NULL;
    
    inode *parent = &root;
    asdfs_errno return_code = NO_ERROR;

    if (strcmp(path, "/")==0) {
        res->exact = &root;
        return_code = EXACT_FOUND;
        return_code |= can_read(&root) ? CAN_READ_EXACT : 0;
        return_code |= can_write(&root) ? CAN_WRITE_PARENT : 0;
        return_code |= can_execute(&root) ? CAN_EXECUTE_EXACT : 0;
        return return_code;
    }
    
    unsigned long length = strlen(path);
    char *tok_path = (char *)malloc(strlen(path) + 1);
    if (tok_path == NULL) {
        return GENERAL_ERROR;
    }
    strncpy(tok_path, path, length + 1);
    
    char * curr_comp = strtok(tok_path, "/");
    
    while (curr_comp!=NULL) {
        if (!(parent->attr.st_mode & S_IFDIR)) {
            return HEAD_NOT_DIRECTORY;
        }

        // superuser가 아닌 사용자면서 상위 폴더에 EXECUTE 권한이 없을 경우 탐색 불가
        if (!is_root() && !can_execute(parent)) {
            return HEAD_NO_PERMISSION;
        }
        
        char * next_comp = strtok(NULL, "/");
        
        return_code=child_search(parent, curr_comp, res);

        if (return_code==EXACT_FOUND) {
            parent = res->exact;
        }
        else if (next_comp) {
            free(tok_path);
            return HEAD_NOT_FOUND;
        }
        curr_comp = next_comp;
    }

    free(tok_path);

    if (res->parent) {
        return_code |= can_read(res->parent) ? CAN_READ_PARENT : 0;
        return_code |= can_write(res->parent) ? CAN_WRITE_PARENT : 0;
        return_code |= can_execute(res->parent) ? CAN_EXECUTE_PARENT : 0;
    }

    if (res->exact) {
        return_code |= can_read(res->exact) ? CAN_READ_EXACT : 0;
        return_code |= can_write(res->exact) ? CAN_WRITE_EXACT : 0;
        return_code |= can_execute(res->exact) ? CAN_EXECUTE_EXACT : 0;

        if (res->exact->attr.st_uid == fuse_get_context()->uid) {
            return_code |= IS_OWNER;
        }
    }

    return return_code;
}

asdfs_errno create_inode(const char *path, struct stat attr, inode **out) {
    unsigned long length = strlen(path);
    char *tok_path = (char *)malloc(strlen(path) + 1);
    if (tok_path == NULL) {
        return GENERAL_ERROR;
    }
    strncpy(tok_path, path, length + 1);
    
    char *curr_comp = strtok(tok_path, "/");
    while (curr_comp!=NULL) {
        char *next_comp = strtok(NULL, "/");
        if (next_comp==NULL) {
            break;
        }
        curr_comp = next_comp;
    }

    // 파일 시스템 남은 블럭 수 계산
    unsigned long block_size = superblock.f_bsize;
    unsigned long inodes_per_block = block_size / INODE_SIZE_BYTE;
    fsfilcnt_t remainder = superblock.f_files % inodes_per_block;
    if (remainder == 0) { // 새로운 블럭 할당 필요
        if (superblock.f_bfree == 0) {
            return NO_FREE_SPACE;
        }

        superblock.f_bfree--;
    }
    superblock.f_files++; // 파일 개수 증가

    inode *new = (inode*)calloc(1, sizeof(inode));
    new->attr = attr;
    attr.st_ino = (uint64_t)new;

    strncpy(new->name, curr_comp, MAX_FILENAME + 1);
    free(tok_path);
    *out = new;
    return NO_ERROR;
}

asdfs_errno alloc_data_inode(inode *node, off_t size) {
    unsigned long block_size = superblock.f_bsize;

    off_t curr_size = node->attr.st_size;
    blkcnt_t curr_blocks = node->attr.st_blocks;

    off_t new_size = size;
    blkcnt_t new_blocks = (new_size / block_size) + !!(new_size % block_size);

    fsblkcnt_t f_bfree = superblock.f_bfree;
    f_bfree += curr_blocks;
    if (f_bfree < new_blocks) {
        return NO_FREE_SPACE;
    }
    f_bfree -= new_blocks; 

    void *data = node->data;
    if (data == NULL) {
        data = calloc(1, new_blocks * block_size);
    } else if (curr_blocks != new_blocks) {
        data = realloc(data, new_blocks * block_size);
    }
    node->data = data;

    if (data == NULL) {
        return GENERAL_ERROR;
    }

    node->attr.st_size = new_size;
    node->attr.st_blocks = new_blocks;

    superblock.f_bfree = f_bfree;
    return NO_ERROR;
}

void dealloc_data_inode(inode *node) {
    if (node->data) {
        free(node->data);
    }

    superblock.f_bfree += node->attr.st_blocks;
}

void insert_inode(inode *parent, inode *left, inode *right, inode *new) {
    if (parent == NULL) {
        return;
    }
    
    new->parent = parent;
    
    if (left != NULL && right != NULL){
        left->rightSibling = new;
        right->leftSibling = new;
    
        new->leftSibling = left;
        new->rightSibling = right;
    }

    if (left == NULL && right != NULL) {
        right->leftSibling = new;
        new->rightSibling = right;
        
        parent->firstChild = new;
    }
    
    if (right == NULL && left != NULL) {
        left->rightSibling = new;
        new->leftSibling = left;
        
        parent->lastChild = new;
    }
    
    if (left == NULL && right ==NULL){
        parent->firstChild = new;
        parent->lastChild = new;
    }
}

void extract_inode(inode *node) {
    if (node == NULL) {
        return;
    }
    
    inode *parent = node->parent;
    inode *left = node->leftSibling;
    inode *right = node->rightSibling;
    
    if (left != NULL && right != NULL){
        left->rightSibling = right;
        right->leftSibling = left;
    }
    
    if (left == NULL && right != NULL) {
        right->leftSibling = left;
        
        parent->firstChild = right;
    }
    
    if (right == NULL && left != NULL) {
        left->rightSibling = right;
        
        parent->lastChild = left;
    }
    
    if (parent != NULL && left == NULL && right == NULL){
        parent->firstChild = NULL;
        parent->lastChild = NULL;
    }
    
    node->parent = NULL;
}

void destroy_inode(inode *node) {
    if (node == NULL) {
        return;
    }

    while (node->firstChild) {
        destroy_inode(node->firstChild);
    }
    
    extract_inode(node);
    dealloc_data_inode(node);

    if (node != &root) {
        free(node);
    }

    // 파일 시스템 남은 블럭 수 계산
    unsigned long block_size = superblock.f_bsize;
    unsigned long inodes_per_block = block_size / INODE_SIZE_BYTE;
    fsfilcnt_t remainder = superblock.f_files % inodes_per_block;
    if (remainder == 1) { // 기존 블럭 해제 필요
        superblock.f_bfree--;
    }
    superblock.f_files--; // 파일 개수 감소
}

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn) {
    fprintf(stderr, "asdfs_init\n");

    init_inode();
    return NULL;
}

// 파일 시스템 정보 조회
int asdfs_statfs (const char *path, struct statvfs *buf) {
    fprintf(stderr, "asdfs_statfs\n");

    *buf = superblock;
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
    fprintf(stderr, "asdfs_read %s %zu %lld\n", path, size, off);

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
    fprintf(stderr, "asdfs_truncate %s %lld\n", path, size);

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
    fprintf(stderr, "asdfs_write %s %zu %lld\n", path, size, off);

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

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &asdfs_oper, NULL);
}
