#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>

#define MAX_ARGV 32

void run(const char* path, ...) {
	pid_t pid = fork();

	if (pid == 0) {
		char* argv[MAX_ARGV];
		argv[0] = (char*)path;

		va_list args;
		va_start(args, path);

		int i = 1;
		char* arg;
		
		while ((arg = va_arg(args, char*)) != NULL && i < MAX_ARGV) {
			argv[i++] = arg;
		}
		argv[i] = NULL;
		va_end(args);

		execve(path, argv, NULL);
		exit(0);
	}
	sleep(1);
}

int main(void) {
	setsid();

	run("/sbin/vcomp", NULL);
	run("/sbin/vxterm", NULL);

	return 0;
}