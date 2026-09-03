#include "toolbox.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

void mode_ls(int argc, char* argv[]) {
    char * target_dir = ".";
    if (argc > 1) {
        target_dir = argv[1];
    }
    
    DIR* dir = opendir(target_dir);
    if (!dir) {
        fprintf(stderr, "unable to open directory\n");
        exit(-1);
    }

    struct dirent *entry;
    // Read each directory entry sequentially
    while ((entry = readdir(dir)) != NULL) {
        // Hide hidden files (starting with '.') by default
        if (entry->d_name[0] != '.') {
            printf("%s  ", entry->d_name);
        }
    }
    printf("\n");

    // Close the stream to release resources
    closedir(dir);
}