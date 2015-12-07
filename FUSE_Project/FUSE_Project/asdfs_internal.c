#include "asdfs_internal.h"

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

struct statvfs get_superblock() {
	return superblock;
}

asdfs_errno find_inode(const char *path, search_result *res) {
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
        superblock.f_bavail--;
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
    } else if (size == 0) {
        free(data);
        data = calloc(1, 0);        
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
    superblock.f_bavail = f_bfree;
    return NO_ERROR;
}

void dealloc_data_inode(inode *node) {
    if (node->data) {
        free(node->data);
    }

    fsblkcnt_t f_bfree = superblock.f_bfree;
    f_bfree += node->attr.st_blocks;
    superblock.f_bfree = f_bfree;
    superblock.f_bavail = f_bfree;
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
        superblock.f_bavail--;
    }
    superblock.f_files--; // 파일 개수 감소
}
