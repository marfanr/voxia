#include "toolbox.h"
#include <stdio.h>
#include <sys/mount.h>

void mode_mount(int argc, char* argv[]) {
	if (argc < 3) {
		printf("toolbox: mount [source] [target]\n");
		return;
	}

	char* source = argv[1];
	char* target = argv[2];

	int res = mount(source, target, "FAT32", MS_BIND, 0);
	if (res == -1) {
		perror("mount error");
		return;
	}
}

void mode_umount(int argc, char* argv[]) {
	if (argc < 2) {
		printf("toolbox: umount [source] / [target]\n");
		return;
	}

	char* source = argv[1];
	
	int res = umount(source);
	if (res == -1) {
		perror("mount error");
		return;
	}
}