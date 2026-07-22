#include "toolbox.h"
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int current_mode = 0;

enum { MODE_NONE = 0, MODE_MOUNT, MODE_UNMOUNT, MODE_LS, MODE_ECHO };

static int detect_mode(int argc, char* argv[], const char* name) {
	char* cmd = basename(argv[0]);

	// dipanggil langsung: ./ls
	if (strcmp(cmd, name) == 0)
		return 0;

	// dipanggil: ./toolbox ls
	if (argc > 1 && strcmp(argv[1], name) == 0)
		return 1;

	return -1;
}

static void mode_select(int* argc, char*** argv) {
	struct {
		const char* name;
		int id;
	} modes[] = {
	    {"mount", MODE_MOUNT},
	    {"umount", MODE_UNMOUNT},
	    {"ls", MODE_LS},
	    {"echo", MODE_ECHO},
	};

	for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {

		int ret = detect_mode(*argc, *argv, modes[i].name);

		if (ret >= 0) {
			current_mode = modes[i].id;

			if (ret == 1) {
				(*argc)--;
				(*argv)++;
			}

			return;
		}
	}
}

static void bumper(void) {
	printf("Voxia Toolbox v0.0.1\n"
	       "Toolbox is copyrighted by Mohammad Arfan.\n"
	       "Licensed under GPL-3.0.\n\n"

	       "Usage: toolbox [function [arguments]...]\n"
	       "   or: function [arguments]...\n\n"

	       "Available functions:\n"
	       "  ls\n"
	       "  mount\n"
	       "  umount\n"
	       "  echo\n");
}

int main(int argc, char** argv) {
	mode_select(&argc, &argv);

	switch (current_mode) {

	case MODE_MOUNT:
		mode_mount(argc, argv);
		break;

	case MODE_UNMOUNT:
		mode_umount(argc, argv);
		break;

	case MODE_LS:
		mode_ls(argc, argv);
		break;

	case MODE_ECHO:
		// mode_echo(argc, argv);
		break;

	default:
		bumper();
		break;
	}

	return 0;
}