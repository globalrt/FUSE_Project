#include "asdfs_internal.h"

static struct statvfs superblock; // 파일 시스템 메타데이터
static inode root;                // 최초 root inode

// 호출 프로세스가 superuser인지 반환
int is_root() {
    struct fuse_context *context = fuse_get_context();
    return context->uid == 0;
}

// 호출 프로세스가 해당 inode에 읽기 권한이 있는지 확인
int can_read(inode *node) {
    // 현재 fuse context, 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    // node의 파일 권한, 소유자 uid, 그룹 gid.
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

    // 그 외 others이고, 파일에 others READ 권한이 있는 경우
    if (file_mode & S_IROTH) {
        return 1;
    }

    return 0;
}

// 호출 프로세스가 해당 inode에 쓰기 권한이 있는지 확인
int can_write(inode *node) {
    // 현재 fuse context, 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    // node의 파일 권한, 소유자 uid, 그룹 gid.
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

// 호출 프로세스가 해당 inode에 실행/탐색 권한이 있는지 확인
int can_execute(inode *node) {
    // 현재 fuse context, 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();
    uid_t curr_uid = context->uid;
    uid_t curr_gid = context->gid;

    // node의 파일 권한, 소유자 uid, 그룹 gid.
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

// parent의 firstChild부터 lastChild까지 검색하여
// search_name의 위치 정보 또는 search_name이 들어갈 위치 정보를
// search_result에 기록하여 res 포인터로 반환
asdfs_errno child_search(inode *parent, const char *search_name, search_result *res){
    if (parent == NULL || search_name == NULL || res == NULL) {
        return GENERAL_ERROR;
    }
    
    res->parent = parent;
    
    // parent에 child가 없는 경우
    inode *firstChild = parent->firstChild;
    if(parent->firstChild==NULL){
        // 위치 정보 반환
        res->left=NULL;
        res->exact=NULL;
        res->right=NULL;
        
        // 주어진 위치에 inode 없음
        return EXACT_NOT_FOUND;
    }
    
    // firstChild보다 ABC순으로 앞에 위치하는 경우
    if (strcmp(search_name,firstChild->name)<0) {
        // 위치 정보 반환
        res->left = NULL;
        res->exact = NULL;
        res->right = firstChild;

        // 주어진 위치에 inode 없음
        return EXACT_NOT_FOUND;
    }


    // lastChild보다 ABC순으로 뒤에 위치하는 경우
    inode *lastChild = parent->lastChild;
    if (strcmp(search_name,lastChild->name)>0) {
        // 위치 정보 반환
        res->left = lastChild;
        res->exact = NULL;
        res->right = NULL;

        // 주어진 위치에 inode 없음
        return EXACT_NOT_FOUND;
    }
    
    // firstChild부터 ABC순으로
    // 직전에 위치하지 않는 child inode가 나올 때까지 반복
    inode *child = firstChild;
    while (strcmp(search_name,child->name)>0) {
        child = child->rightSibling;
    }
    
    // child가 search_name과 같은 경우
    if (strcmp(search_name,child->name)==0) {
        // 위치 정보 반환
        res->left = child->leftSibling;
        res->exact = child;
        res->right = child->rightSibling;

        // 주어진 위치에 inode 있음
        return EXACT_FOUND;
    }
    // 같지 않은 경우
    else {
        // 위치 정보 반환
        res->left = child->leftSibling;
        res->exact = NULL;
        res->right = child;

        // 주어진 위치에 inode 없음
        return EXACT_NOT_FOUND;
    }
}

// 파일 시스템 root inode, superblock 초기화
void init_root_superblock() {
    // 현재 fuse context 가져오기. 호출 프로세스의 uid, gid.
    struct fuse_context *context = fuse_get_context();

    // 현재 시간 가져오기
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

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
    unsigned long f_bsize = BLOCK_SIZE_KB * 1024; // 4096
    
    // f_blocks: 파일 시스템 내 전체 블록 개수
    fsblkcnt_t f_blocks = VOLUME_SIZE_MB * 1024 / BLOCK_SIZE_KB; // 25600
    
    // f_favail: 사용 가능한 파일 시리얼 넘버 (inode) 개수
    fsfilcnt_t f_favail = (fsfilcnt_t)(f_blocks * (f_bsize / INODE_SIZE_BYTE)); // 204800

    superblock.f_bsize   = f_bsize;      // 파일 시스템 블록 크기
    superblock.f_blocks  = f_blocks;     // 파일 시스템 내 전체 블록 개수
    superblock.f_bfree   = f_blocks - 1; // 사용 가능한 블록 수, root만큼 제외
    superblock.f_bavail  = f_blocks - 1; // 일반 권한 프로세스가 사용 가능한 블록 수, root만큼 제외
    superblock.f_files   = 0;            // 전체 파일 시리얼 넘버 (inode) 개수
    superblock.f_favail  = f_favail - 1; // 사용 가능한 파일 시리얼 넘버 (inode) 개수, root만큼 제외
    superblock.f_namemax = MAX_FILENAME; // 최대 파일 이름 길이
    // 나머지 값은 static이므로 전부 0.
}

// 파일 시스템 superblock 정보 반환
struct statvfs get_superblock() {
	return superblock;
}

// path에 해당하는 inode 검색, 결과 res 포인터로 반환
asdfs_errno find_inode(const char *path, search_result *res) {
    if (res == NULL) {
        return GENERAL_ERROR;
    }

    // res 초기화
    res->parent = NULL;
    res->left = NULL;
    res->exact = NULL;
    res->right = NULL;
    
    // root를 찾는 경우
    if (strcmp(path, "/")==0) {
        // 위치 정보 반환
        res->exact = &root;

        // 주요 오류 번호 및 비트 마스크 반환
        asdfs_errno return_code = EXACT_FOUND;
        return_code |= can_read(&root) ? CAN_READ_EXACT : 0;
        return_code |= can_write(&root) ? CAN_WRITE_PARENT : 0;
        return_code |= can_execute(&root) ? CAN_EXECUTE_EXACT : 0;
        return return_code;
    }
    
    // parent는 root에서 시작
    inode *parent = &root;
    asdfs_errno return_code = NO_ERROR;

    // 문자열 path를 tok_path로 복사
    unsigned long length = strlen(path);
    char *tok_path = (char *)malloc(strlen(path) + 1);
    if (tok_path == NULL) {
        return GENERAL_ERROR;
    }
    strncpy(tok_path, path, length + 1);

    // strtok을 사용하여 "/" 단위로 가져온 첫번째 path component
    char * curr_comp = strtok(tok_path, "/");    
    while (curr_comp != NULL) {
        // parent가 디렉터리가 아닌 경우 탐색 불가
        if (!(parent->attr.st_mode & S_IFDIR)) {
            // tree path 중간에 디렉터리가 아닌 inode 있음
            return HEAD_NOT_DIRECTORY;
        }

        // superuser가 아닌 사용자면서 상위 폴더에 EXECUTE 권한이 없을 경우 탐색 불가
        if (!is_root() && !can_execute(parent)) {
            // tree path 탐색 권한 없음
            return HEAD_NO_PERMISSION;
        }
        
        // parent 아래에 curr_comp에 해당하는 inode가 있는지 검색
        return_code = child_search(parent, curr_comp, res);

        // 다음 처리할 path component
        char *next_comp = strtok(NULL, "/");
        
        // curr_comp inode가 있었을 경우,
        if (return_code == EXACT_FOUND) {
            // parent를 exact로 교체
            parent = res->exact;
        }
        // curr_comp inode가 없었으나 다음 처리할 path component가 남은 경우
        else if (next_comp != NULL) {
            free(tok_path);
            // 주어진 위치로의 tree path 없음
            return HEAD_NOT_FOUND;
        }

        // next_comp로 진행
        curr_comp = next_comp;
    }

    free(tok_path);

    // parent를 찾은 경우 해당하는 보조 비트 마스크 적용
    if (res->parent) {
        return_code |= can_read(res->parent) ? CAN_READ_PARENT : 0;
        return_code |= can_write(res->parent) ? CAN_WRITE_PARENT : 0;
        return_code |= can_execute(res->parent) ? CAN_EXECUTE_PARENT : 0;
    }

    // exact 찾은 경우 해당하는 보조 비트 마스크 적용
    if (res->exact) {
        return_code |= can_read(res->exact) ? CAN_READ_EXACT : 0;
        return_code |= can_write(res->exact) ? CAN_WRITE_EXACT : 0;
        return_code |= can_execute(res->exact) ? CAN_EXECUTE_EXACT : 0;

        // 호출 프로세스가 exact의 소유자인 경우
        if (res->exact->attr.st_uid == fuse_get_context()->uid) {
            return_code |= IS_OWNER;
        }
    }

    return return_code;
}

// 새로운 inode 생성, res 포인터로 반환
asdfs_errno create_inode(const char *path, struct stat attr, inode **out) {
    // 문자열 path를 tok_path로 복사
    unsigned long length = strlen(path);
    char *tok_path = (char *)malloc(strlen(path) + 1);
    if (tok_path == NULL) {
        return GENERAL_ERROR;
    }
    strncpy(tok_path, path, length + 1);
    
    // strtok을 사용하여 "/" 단위로 가져온 첫번째 path component
    char *curr_comp = strtok(tok_path, "/");
    while (curr_comp!=NULL) {
        // 다음 처리할 path component
        char *next_comp = strtok(NULL, "/");
        // next_comp가 없으면 중단
        if (next_comp == NULL) {
            break;
        }
        // next_comp로 진행
        curr_comp = next_comp;
    }
    // 이 때, curr_comp가 파일 이름 (tail)
    if (curr_comp == NULL) {
        return GENERAL_ERROR;
    }

    // 파일 시스템 잔여 블록 수 계산
    unsigned long block_size = superblock.f_bsize;
    unsigned long inodes_per_block = block_size / INODE_SIZE_BYTE;

    // 현재 inode 개수에서 블록 당 inode 개수를 나눈
    // 나머지가 0이면 새로운 블록 할당 필요
    fsfilcnt_t remainder = superblock.f_files % inodes_per_block;
    if (remainder == 0) {
        // 이 때 남은 블록이 없다면 
        if (superblock.f_bfree == 0) {
            // 파일 시스템에 남은 용량 없음
            return NO_FREE_SPACE;
        }

        // 사용 가능한 블록 수 감소
        superblock.f_bfree--;
        superblock.f_bavail--;
    }
    // 파일 개수 증가
    superblock.f_files++;

    // 새로운 inode 메모리 할당
    inode *new = (inode*)calloc(1, sizeof(inode));
    new->attr = attr;
    attr.st_ino = (uint64_t)new; // 파일 시리얼 넘버는 포인터 값 사용

    // 파일 이름 복사
    strncpy(new->name, curr_comp, MAX_FILENAME + 1);

    // res 포인터로 new 반환
    *out = new;

    free(tok_path);
    return NO_ERROR;
}

// node에 data 공간 할당
asdfs_errno alloc_data_inode(inode *node, off_t size) {
    unsigned long block_size = superblock.f_bsize;

    // 현재 node에 할당된 공간
    off_t curr_size = node->attr.st_size;
    blkcnt_t curr_blocks = node->attr.st_blocks;

    // 요청에 따라 node에 할당될 공간
    off_t new_size = size;
    blkcnt_t new_blocks = (new_size / block_size) + !!(new_size % block_size);

    // 현재 파일 시스템 잔여 블록 수에서
    fsblkcnt_t f_bfree = superblock.f_bfree;
    // node에 할당된 블록을 반환한 수가
    f_bfree += curr_blocks;
    // 요청에 따라 node에 할당될 블록 수보다 적으면
    if (f_bfree < new_blocks) {
        // 파일 시스템에 남은 용량 없음
        return NO_FREE_SPACE;
    }
    // 할당될 블록 수 감산
    f_bfree -= new_blocks; 

    void *data = node->data;
    // data가 할당되지 않은 경우 할당
    if (data == NULL) {
        data = calloc(1, new_blocks * block_size);
    }
    // data가 이전에 할당되었으나 요청 공간이 0인 경우
    else if (size == 0) {
        // 기존 data를 반환하고
        free(data);
        // 새로운 0 크기의 메모리 할당
        data = calloc(1, 0);        
    }
    // data가 이전에 할당되었고 요청 공간이 현재 공간과 크기가 다른 경우
    else if (curr_blocks != new_blocks) {
        // 할당된 메모리 크기 조정
        data = realloc(data, new_blocks * block_size);
    }
    node->data = data;

    if (data == NULL) {
        return GENERAL_ERROR;
    }

    // 새롭게 할당된 공간 크기 반영
    node->attr.st_size = new_size;
    node->attr.st_blocks = new_blocks;

    // 새롭게 계산된 파일 시스템 잔여 블록 수 반영
    superblock.f_bfree = f_bfree;
    superblock.f_bavail = f_bfree;
    return NO_ERROR;
}

// node의 data 공간 반환
void dealloc_data_inode(inode *node) {
    // data 메모리 반환
    if (node->data) {
        free(node->data);
    }

    // 현재 파일 시스템 잔여 블록 수 계산하여 반영
    fsblkcnt_t f_bfree = superblock.f_bfree;
    f_bfree += node->attr.st_blocks;
    superblock.f_bfree = f_bfree;
    superblock.f_bavail = f_bfree;
}

// 새로운 inode를 res 위치에 삽입
void insert_inode(search_result res, inode *new) {
    inode *parent = res.parent;
    inode *left = res.left;
    inode *right = res.right;

    // parent가 없을 경우 무시
    if (parent == NULL) {
        return;
    }
    
    new->parent = parent;
    
    // left 없고 right 없음
    if (left == NULL && right ==NULL){
        parent->firstChild = new;
        parent->lastChild = new;
    }

    // left 없고 right 있음
    if (left == NULL && right != NULL) {
        right->leftSibling = new;
        new->rightSibling = right;
        
        parent->firstChild = new;
    }
    
    // left 있고 right 없음
    if (left != NULL && right == NULL) {
        left->rightSibling = new;
        new->leftSibling = left;
        
        parent->lastChild = new;
    }

    // left 있고 right 있음
    if (left != NULL && right != NULL){
        left->rightSibling = new;
        right->leftSibling = new;
    
        new->leftSibling = left;
        new->rightSibling = right;
    }
}

// node를 inode tree에서 분리
void extract_inode(inode *node) {
    if (node == NULL) {
        return;
    }
    
    inode *parent = node->parent;
    inode *left = node->leftSibling;
    inode *right = node->rightSibling;
    
    // root를 제외하고, left 없고 right 없음
    if (parent != NULL && left == NULL && right == NULL){
        parent->firstChild = NULL;
        parent->lastChild = NULL;
    }

    // left 없고 right 있음    
    if (left == NULL && right != NULL) {
        right->leftSibling = left;
        
        parent->firstChild = right;
    }
    
    // left 있고 right 없음
    if (left != NULL && right == NULL) {
        left->rightSibling = right;
        
        parent->lastChild = left;
    }
    
    // left 있고 right 있음
    if (left != NULL && right != NULL){
        left->rightSibling = right;
        right->leftSibling = left;
    }
    
    // parent 해제
    node->parent = NULL;
}

// inode 삭제
void destroy_inode(inode *node) {
    if (node == NULL) {
        return;
    }

    // 재귀적으로 firstChild 삭제
    while (node->firstChild) {
        destroy_inode(node->firstChild);
    }
    
    // node를 inode tree에서 분리
    extract_inode(node);


    // node의 data 공간 반환
    dealloc_data_inode(node);

    // root가 아닌 node에 대해서 메모리 반환
    if (node != &root) {
        free(node);
    }

    // 파일 시스템 잔여 블록 수 계산
    unsigned long block_size = superblock.f_bsize;
    unsigned long inodes_per_block = block_size / INODE_SIZE_BYTE;

    // 현재 inode 개수에서 블록 당 inode 개수를 나눈
    // 나머지가 1이면 기존 블록 해제 필요
    fsfilcnt_t remainder = superblock.f_files % inodes_per_block;
    if (remainder == 1) { 
        // 사용 가능한 블록 수 증가
        superblock.f_bfree++;
        superblock.f_bavail++;
    }
    // 파일 개수 감소
    superblock.f_files--;
}
