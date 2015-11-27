#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>


typedef struct inode inode;



struct inode {

    struct stat attr;
    
    uint8_t name[256];
    
    inode *parent;
    inode *leftSibling;
    inode *rightSibling;
    inode *firstChild;
    inode *lastChild;
    
    void *data;
};

static inode root;



int main(int argc, const char * argv[]) {
    return 1;
}
