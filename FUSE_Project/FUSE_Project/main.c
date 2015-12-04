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
    
    GENERAL_ERROR
} asdfs_errno;

static struct statvfs superblock;
static inode root;

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


asdfs_errno find_inode(const char *path, search_result *res){
    res->parent = NULL;
    res->left = NULL;
    res->exact = NULL;
    res->right = NULL;
    
    inode *parent = &root;
    
    if (strcmp(path, "/")==0) {
        res->exact = &root;
        return EXACT_FOUND;
    }
    
    asdfs_errno return_code = NO_ERROR;
    
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
    return return_code;
}

inode *create_inode(const char *path, struct stat attr) {
    unsigned long length = strlen(path);
    char *tok_path = (char *)malloc(strlen(path) + 1);
    if (tok_path == NULL) {
        return NULL;
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

    inode *new = (inode*)calloc(1, sizeof(inode));
    new->attr = attr;
    
    strncpy(new->name, curr_comp, MAX_FILENAME + 1);
    free(tok_path);
    return new;
}

void alloc_data_inode(inode *node, off_t size) {
    unsigned long block_size = superblock.f_bsize;

    off_t curr_size = node->attr.st_size;
    blkcnt_t curr_blocks = node->attr.st_blocks;

    off_t new_size = size;
    blkcnt_t new_blocks = (new_size / block_size) + !!(new_size % block_size);

    void *data = node->data;
    if (data == NULL) {
        data = calloc(1, new_blocks * block_size);
    } else if (curr_blocks != new_blocks) {
        data = realloc(data, new_blocks * block_size);
    }
    node->data = data;

    node->attr.st_size = new_size;
    node->attr.st_blocks = new_blocks;
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
    
    if (node != &root) {
        free(node);
    }
}

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn) {
    // 현재 시간 가져오기
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    
    // root inode 초기화
    strncpy(root.name, "ROOT", MAX_FILENAME);
    root.attr.st_mode  = S_IFDIR;     // 파일 모드
    root.attr.st_nlink = 1;           // 파일 링크 개수
    root.attr.st_atime = now.tv_sec;  // 파일 최근 사용 시간
    root.attr.st_mtime = now.tv_sec;  // 파일 최근 수정 시간
    root.attr.st_ctime = now.tv_sec;  // 파일 최근 상태 변화 시간
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

// 파일 시스템 정보 조회
int asdfs_statfs (const char *path, struct statvfs *buf) {
    *buf = superblock;
    return 0;
}

// 파일 정보 조회
int asdfs_getattr (const char *path, struct stat *buf) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    
    switch (code) {
        case EXACT_FOUND:
            *buf = res.exact->attr;
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 디렉터리 생성
int asdfs_mkdir (const char *path, mode_t mode) {
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

    inode *node = NULL;
    
    switch (code) {
        case EXACT_FOUND:
            return -EEXIST;
            
        case EXACT_NOT_FOUND:
            node = create_inode(path, attr);
            insert_inode(res.parent, res.left, res.right, node);
            return 0;
            
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 디렉터리 삭제
int asdfs_rmdir (const char *path) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code) {
        case EXACT_FOUND:
            if (res.exact->firstChild) {
                return -ENOTEMPTY;
            }

            destroy_inode(res.exact);
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}
// 디렉터리 열기
int asdfs_opendir (const char *path, struct fuse_file_info *fi) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;
    
    switch (code) {
        case EXACT_FOUND:
            if (!(exact->attr.st_mode & S_IFDIR)) {
                return -ENOTDIR;
            }
            
            fi->fh = (uint64_t)exact;
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 디렉터리 읽기
int asdfs_readdir (const char *path, void *buf, fuse_fill_dir_t filer, off_t off, struct fuse_file_info *fi) {
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
    if (!(mode & S_IFREG)) {
        return -ENOSYS;
    }
    
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    struct fuse_context *context = fuse_get_context();
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

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

    inode *node = NULL;
    
    switch (code) {
        case EXACT_FOUND:
            return -EEXIST;
            
        case EXACT_NOT_FOUND:
            node = create_inode(path, attr);
            insert_inode(res.parent, res.left, res.right, node);
            return 0;
            
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 파일 삭제
int asdfs_unlink (const char *path) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code) {
        case EXACT_FOUND:
            destroy_inode(res.exact);
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 파일 열기
int asdfs_open (const char *path, struct fuse_file_info *fi) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;
    
    switch (code) {
        case EXACT_FOUND:
            if (exact->attr.st_mode & S_IFDIR) {
                return -EISDIR;
            }
            
            fi->fh = (uint64_t)exact;
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

// 생성 및 수정 시간 변경
int asdfs_utimens (const char *path, const struct timespec tv[2]) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);
    inode *exact = res.exact;

    switch (code) {
        case EXACT_FOUND:
            exact->attr.st_atime = tv[0].tv_sec;
            exact->attr.st_mtime = tv[1].tv_sec;
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

int asdfs_read (const char *path, char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }

    void *data = node->data;
    if (data == NULL) {
        return -EIO;
    }

    char *ptr = (char *)data + off;
    for (size_t i=0; i<size; i++) {
        mem[i] = ptr[i];
    }

    return size;
}

int asdfs_truncate (const char *path, off_t size) {
    search_result res;
    asdfs_errno code = find_inode(path, &res);

    switch (code) {
        case EXACT_FOUND:
            alloc_data_inode(res.exact, size);
            return 0;
            
        case EXACT_NOT_FOUND:
        case HEAD_NOT_FOUND:
            return -ENOENT;
            
        case HEAD_NOT_DIRECTORY:
            return -ENOTDIR;
            
        case GENERAL_ERROR:
        default:
            return -EIO;
    }
}

int asdfs_write (const char *path, const char *mem, size_t size, off_t off, struct fuse_file_info *fi) {
    inode *node = (inode *)fi->fh;
    if (node == NULL) {
        return -EIO;
    }
    
    alloc_data_inode(node, max(node->attr.st_size, off + size));

    char *ptr = (char *)(node->data) + off;
    for (size_t i=0; i<size; i++) {
        ptr[i] = mem[i];
    }

    return size;
}

#warning The code block below is only for debugging. Remove it before the submision!

// OS X 환경에서만 컴파일 되는 코드 블럭
#ifdef __APPLE__

#include <assert.h>

#define ASDFS_ERRNO_STRING(n) (n==NO_ERROR?"NO_ERROR": \
                                (n==EXACT_FOUND?"EXACT_FOUND": \
                                (n==EXACT_NOT_FOUND?"EXACT_NOT_FOUND": \
                                (n==HEAD_NOT_DIRECTORY?"HEAD_NOT_DIRECTORY": \
                                (n==HEAD_NOT_FOUND?"HEAD_NOT_FOUND": \
                                ("GENERAL_ERROR"))))))

int main(int argc, char *argv[]) {
    asdfs_init(NULL);
    
    struct stat dir_attr;
    dir_attr.st_uid = 1000;
    dir_attr.st_gid = 1000;
    dir_attr.st_mode = S_IFDIR | 0600;
    
    struct stat file_attr;
    file_attr.st_uid = 1000;
    file_attr.st_gid = 1000;
    file_attr.st_mode = S_IFREG | 0777;
    
    char p_a[] = "/a";          // DIRECTORY
    char p_c[] = "/c";          // DIRECTORY
    char p_b[] = "/b";          // FILE
    char p_a_b[] = "/a/b";      // FILE
    char p_a_b_c[] = "/a/b/c";  // ERROR: /a/b is NOT DIRECTORY
    
    search_result res;
    asdfs_errno code;
    
    // $ mkdir /a
    fprintf(stderr, "-- $ mkdir %s\n", p_a);
    
    code = find_inode(p_a, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_NOT_FOUND);
    assert(res.parent == &root);
    assert(res.left == NULL);
    assert(res.right == NULL);
    
    inode *i_a = create_inode(p_a, dir_attr);
    assert(i_a != NULL);
    assert(i_a->attr.st_mode == dir_attr.st_mode);

    insert_inode(res.parent, res.left, res.right, i_a);
    fprintf(stderr, "insert_inode\n");
    assert(i_a->parent == &root);
    assert(i_a->leftSibling == NULL);
    assert(i_a->rightSibling == NULL);
    
    fprintf(stderr, "\n");
    
    // $ mkdir /c
    fprintf(stderr, "-- $ mkdir %s\n", p_c);
    
    code = find_inode(p_c, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_NOT_FOUND);
    assert(res.parent == &root);
    assert(res.left == i_a);
    assert(res.right == NULL);
    
    inode *i_c = create_inode(p_c, dir_attr);
    assert(i_c != NULL);
    assert(i_c->attr.st_mode == dir_attr.st_mode);
    
    insert_inode(res.parent, res.left, res.right, i_c);
    fprintf(stderr, "insert_inode\n");
    assert(i_c->parent == &root);
    assert(i_c->leftSibling == i_a);
    assert(i_c->rightSibling == NULL);
    
    fprintf(stderr, "\n");
    
    
    // $ mkdir /b
    fprintf(stderr, "-- $ touch %s\n", p_b);
    
    code = find_inode(p_b, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_NOT_FOUND);
    assert(res.parent == &root);
    assert(res.left == i_a);
    assert(res.right == i_c);
    
    inode *i_b = create_inode(p_b, file_attr);
    assert(i_b != NULL);
    assert(i_b->attr.st_mode == file_attr.st_mode);
    
    insert_inode(res.parent, res.left, res.right, i_b);
    fprintf(stderr, "insert_inode\n");
    assert(i_a->parent == &root);
    assert(i_a->leftSibling == NULL);
    assert(i_a->rightSibling == i_b);
    assert(i_b->parent == &root);
    assert(i_b->leftSibling == i_a);
    assert(i_b->rightSibling == i_c);
    
    fprintf(stderr, "\n");

    
    // $ touch /a/b
    fprintf(stderr, "-- $ touch %s\n", p_a_b);
    
    code = find_inode(p_a_b, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_NOT_FOUND);
    assert(res.parent == i_a);
    assert(res.left == NULL);
    assert(res.right == NULL);
    
    inode *i_a_b = create_inode(p_a_b, file_attr);
    assert(i_a_b != NULL);
    assert(i_a_b->attr.st_mode == file_attr.st_mode);
    
    insert_inode(res.parent, res.left, res.right, i_a_b);
    fprintf(stderr, "insert_inode\n");
    assert(i_a_b->parent == i_a);
    assert(i_a_b->leftSibling == NULL);
    assert(i_a_b->rightSibling == NULL);
    
    fprintf(stderr, "\n");


    // $ ls /a/b
    fprintf(stderr, "-- $ ls %s\n", p_a_b);
    
    code = find_inode(p_a_b, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_FOUND);
    
    fprintf(stderr, "\n");

    
    // $ touch /a/b/c
    fprintf(stderr, "-- $ touch %s\n", p_a_b_c);
    
    code = find_inode(p_a_b_c, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == HEAD_NOT_DIRECTORY);
    
    fprintf(stderr, "\n");

    
    // $ rmdir a
    fprintf(stderr, "-- $ rmdir %s\n", p_a);
    
    code = find_inode(p_a, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == EXACT_FOUND);
    
    destroy_inode(res.exact);
    fprintf(stderr, "remove_inode\n");
    assert(root.firstChild == i_b);
    assert(root.lastChild == i_c);
    
    fprintf(stderr, "\n");

    
    // $ touch /a/b
    fprintf(stderr, "-- $ touch %s\n", p_a_b);
    
    code = find_inode(p_a_b, &res);
    fprintf(stderr, "find_inode %s\n", ASDFS_ERRNO_STRING(code));
    assert(code == HEAD_NOT_FOUND);
    
    fprintf(stderr, "\n");
    
    destroy_inode(&root);
    
    return 0;
}

// 그 외 환경 (Linux) 에서 컴파일 되는 코드 블럭
#else

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
    .read     = asdfs_read,
    .truncate = asdfs_truncate,
    .write    = asdfs_write,

    .utimens  = asdfs_utimens,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &asdfs_oper, NULL);
}

#endif
