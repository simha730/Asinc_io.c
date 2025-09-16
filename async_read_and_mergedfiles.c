#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <aio.h>

#define BUF_SIZE 8192  // 8KB per chunk

typedef struct {
    int fd;
    struct aiocb cb;
    char *buffer;
    int active;
} FileTask;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file1> <file2> ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int nfiles = argc - 1;
    FileTask *tasks = calloc(nfiles, sizeof(FileTask));
    if (!tasks) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    // Initialize each file task
    for (int i = 0; i < nfiles; i++) {
        tasks[i].fd = open(argv[i+1], O_RDONLY);
        if (tasks[i].fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        tasks[i].buffer = malloc(BUF_SIZE);
        if (!tasks[i].buffer) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        memset(&tasks[i].cb, 0, sizeof(struct aiocb));
        tasks[i].cb.aio_fildes = tasks[i].fd;
        tasks[i].cb.aio_buf = tasks[i].buffer;
        tasks[i].cb.aio_nbytes = BUF_SIZE;
        tasks[i].cb.aio_offset = 0;
        tasks[i].active = 1;

        if (aio_read(&tasks[i].cb) == -1) {
            perror("aio_read");
            exit(EXIT_FAILURE);
        }
    }

    const struct aiocb **cb_list = malloc(sizeof(struct aiocb*) * nfiles);
    int remaining = nfiles;

    while (remaining > 0) {
        for (int i = 0; i < nfiles; i++)
            cb_list[i] = tasks[i].active ? &tasks[i].cb : NULL;

        // Wait until at least one AIO completes
        if (aio_suspend(cb_list, nfiles, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("aio_suspend");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfiles; i++) {
            if (!tasks[i].active) continue;

            int err = aio_error(&tasks[i].cb);
            if (err == EINPROGRESS) continue;

            int ret = aio_return(&tasks[i].cb);
            if (ret > 0) {
                // Write exactly what was read
                if (write(STDOUT_FILENO, tasks[i].buffer, ret) == -1) {
                    perror("write");
                    exit(EXIT_FAILURE);
                }

                // Queue next chunk
                tasks[i].cb.aio_offset += ret;
                tasks[i].cb.aio_nbytes = BUF_SIZE;

                if (aio_read(&tasks[i].cb) == -1) {
                    perror("aio_read next chunk");
                    tasks[i].active = 0;
                    close(tasks[i].fd);
                    free(tasks[i].buffer);
                    remaining--;
                }

            } else {
                // EOF or error
                tasks[i].active = 0;
                close(tasks[i].fd);
                free(tasks[i].buffer);
                remaining--;
            }
        }
    }

    free(tasks);
    free(cb_list);
    return 0;
}
