#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TTY_DEV "/dev/tty0"
#define MAX_INPUT 1024

static void term_write(const char* s, size_t len) { write(1, s, len); }
static void term_puts(const char* s) { term_write(s, strlen(s)); }

static int term_read_key(int fd, char out[8]) {
	char c;
	int n = (int)read(fd, &c, 1);
	if (n <= 0)
		return -1;

	/* Escape sequence (arrow keys, etc) */
	if (c == '\033') {
		char seq[6] = {0};
		int sn = (int)read(fd, seq, sizeof(seq) - 1);
		if (sn >= 2 && seq[0] == '[') {
			out[0] = '\033';
			out[1] = '[';
			out[2] = seq[1];
			out[3] = '\0';
			switch (seq[1]) {
			case 'A': /* UP */
				return 0;
			case 'B': /* DOWN */
				return 0;
			case 'C': /* RIGHT */
				return 0;
			case 'D': /* LEFT */
				return 0;
			}
		}
		return 0; /* unknown escape, ignore */
	}

	out[0] = c;
	return 1;
}

int main(void) {
	printf("VXTerminal on TTY0\n");
	printf("Welcome to VOXIA V.0.0.1\n\n");

	char linebuf[MAX_INPUT];
	int linelen = 0;

	while (1) {
		term_puts("$ ");
		linelen = 0;
		memset(linebuf, 0, sizeof(linebuf));

		while (1) {
			char key[8] = {0};
			int n = term_read_key(0, key);

			if (n < 0)
				goto done; /* EOF */

			if (n == 0)
				continue; /* escape sequence di-ignore */

			char c = key[0];

			/* enter */
			if (c == '\n' || c == '\r') {
				break;
			}

			/* Backspace */
			if (c == '\b' || c == 127) {
				if (linelen > 0) {
					linelen--;
					linebuf[linelen] = '\0';
					term_write("\b \b", 3);
				}
				continue;
			}

			if ((unsigned char)c < 0x20)
				continue;

			if (linelen < MAX_INPUT - 1) {
				linebuf[linelen++] = c;
			}
		}

		if (linelen == 0)
			continue;

		pid_t pid = fork();

		if (pid == 0) {
			// TODO: chang eto malloc
			char* argv[10];
			int argc = 0;

			char* s = linebuf;
			char* start = s;

			for (char* p = s;; p++) {
				if (*p == ' ' || *p == '\0') {
					if (argc < 10) {
						argv[argc++] = start;
					}
					if (*p == '\0')
						break;

					*p = '\0';
					start = p + 1;
				}
			}

			int res = execve(linebuf, argv, NULL);
			if (res < 0) {
				printf("command not found: %s\n", linebuf);
				exit(1);
			}
			waitpid(res, NULL, 0);
			exit(0);
		} else if (pid > 0) {
			waitpid(pid, NULL, 0);
		}

		// exit(0);
		// } else if (pid > 0) {
		// 	waitpid(pid, NULL, 0);
		// } else {
		// 	term_puts("fork failed\n");
		// }
	}

done:
	return 0;
}