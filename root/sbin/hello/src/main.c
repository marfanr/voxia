#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    printf("on TTY0 device\n");

    while (1) {
        /* Tampilkan prompt */
        write(1, "$ ", 2);

        /* Baca input */
        char buf[1024];
        memset(buf, 0, sizeof(buf));

        int n = (int)read(0, buf, sizeof(buf) - 1);
        if (n <= 0)
            break;

        /* Hapus newline di akhir */
        if (buf[n - 1] == '\n')
            buf[n - 1] = '\0';

        /* Skip kalau kosong */
        if (buf[0] == '\0')
            continue;

        printf("ok\n");

        /* Jalankan command sederhana */
        // pid_t pid = fork();
        // if (pid == 0) {
        //     /* Child: jalankan command */
        //     char *argv[] = { buf, NULL };
        //     execve(buf, argv, NULL);

        //     /* Kalau execve gagal */
        //     printf("command not found: %s\n", buf);
        //     exit(1);
        // } else if (pid > 0) {
        //     /* Parent: tunggu child selesai */
        //     waitpid(pid, NULL, 0);
        // } else {
        //     printf("fork failed\n");
        // }
    }

    return 0;
}