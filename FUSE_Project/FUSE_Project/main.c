#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>


typedef enum {
    NO_ERROR = 0,
    
    EXACT_FOUND,
    EXACT_NOT_FOUND,
    PATH_NOT_FOUND,
    
    GENERAL_ERROR
} asdfs_errno;

typedef struct inode inode;



struct inode {

    struct stat attr;
    
    char name[256];
    
    inode *parent;
    inode *leftSibling;
    inode *rightSibling;
    inode *firstChild;
    inode *lastChild;
    
    void *data;
};

static inode root;

typedef struct search_result search_result;

struct search_result{
    
    inode *left_inode;
    inode *exact_inode;
    inode *right_inode;
    
    inode *parent;
    
};



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

int main(int argc, const char * argv[]) {
    return 1;
}
