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
struct search_result{
    
    inode *left_inode;
    inode *exact_inode;
    inode *right_inode;
    
    inode *parent;
    
};

typedef enum {
    NO_ERROR = 0,
    
    EXACT_FOUND,
    EXACT_NOT_FOUND,
    PATH_NOT_FOUND,
    
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
        res->left_inode=NULL;
        res->exact_inode=NULL;
        res->right_inode=NULL;
        
        return EXACT_NOT_FOUND; // no child.
    }
    
    
    inode *temp = parent->firstChild;
    
    if (strcmp(search_name,parent->firstChild->name)<0) {
        res->left_inode = NULL;
        res->exact_inode = NULL;
        res->right_inode = temp;
        return EXACT_NOT_FOUND;
        
    }

    if (strcmp(search_name,parent->lastChild->name)>0) {
        res->left_inode = temp;
        res->exact_inode = NULL;
        res->right_inode = NULL;
        return EXACT_NOT_FOUND;
        
    }
    
    while (strcmp(search_name,temp->name)>0) {
        temp = temp->rightSibling;
    }
    
    if (strcmp(search_name,temp->name)==0) {
        res->left_inode = temp->leftSibling;
        res->exact_inode = temp;
        res->right_inode = temp->rightSibling;
        return EXACT_FOUND;
    }
    else{
        res->left_inode = temp->leftSibling;
        res->exact_inode = NULL;
        res->right_inode = temp;
        return EXACT_NOT_FOUND;
    }
    
    
    
}


asdfs_errno find_inode(char *path,search_result *res){
    
    res->parent = NULL;
    res->left_inode = NULL;
    res->exact_inode = NULL;
    res->right_inode = NULL;
    
    inode *parent = &root;
    
    if (strcmp(path, "/")==0) {
        res->exact_inode = &root;
        return EXACT_FOUND;
    }
    
    asdfs_errno return_code = NO_ERROR;
    
    char * current_name = strtok(path, "/");
    
    while (current_name!=NULL) {
        
        char * next_name = strtok(NULL, "/");
        
        return_code=child_search(parent, current_name, res);

        if (return_code==EXACT_FOUND) {
            parent = res->exact_inode;
        }
        else if (next_name) {
            return PATH_NOT_FOUND;
        }

        current_name = next_name;
    }
    
    return return_code;
    
}

inode *create_inode(char *path, struct stat attr) {
    
    char *current_filename = strtok(path, "/");
    
    while (current_filename!=NULL) {
        char *next_filename = strtok(NULL, "/");
        if (next_filename==NULL) {
            break;
        }
        current_filename = next_filename;
    }

    inode *new = (inode*)malloc(sizeof(inode));
    memset(new, 0, sizeof(inode));
    new->attr = attr;
    
    strncpy(new->name, current_filename, 255);
    
    return new;
}

void insert_inode(search_result *res, inode *new) {
    inode *parent = res->parent;
    inode *left = res->left_inode;
    inode *right = res->right_inode;
    
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

void remove_inode(search_result *res) {
    inode *parent = res->parent;
    inode *left = res->left_inode;
    inode *exact = res->exact_inode;
    inode *right = res->right_inode;
    
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
    
    if (left == NULL && right ==NULL){
        parent->firstChild = NULL;
        parent->lastChild = NULL;
    }

    
    free(exact);
}

// 파일 시스템 초기화
void *asdfs_init (struct fuse_conn_info *conn) {
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

static struct fuse_operations asdfs_oper = {
    .init   = asdfs_init
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &asdfs_oper, NULL);
}
