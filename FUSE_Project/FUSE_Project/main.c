#include "asdfs.h"
#include <fuse.h>

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &asdfs_oper, NULL);
}
