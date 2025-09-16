#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <liburing.h>
#include <time.h>

#define QUEUE_DEPTH 64
#define WRITE_SIZE 4096        // 4 KB
#define TOTAL_SIZE (8 * 1024 * 1024) // 8 MB

static inline long long ts_to_ns(const struct timespec *t) {
    return (long long)t->tv_sec * 1000000000LL + t->tv_nsec;
}

int main() {
    const char *filename = "io_uring_async.txt";

    // Open file (without O_DIRECT for WSL compatibility)
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return 1; }

    // Aligned buffer
    unsigned char *buffer = aligned_alloc(4096, WRITE_SIZE);
    if (!buffer) { perror("aligned_alloc"); return 1; }
    memset(buffer, 'A', WRITE_SIZE);

    // Initialize io_uring
    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) { fprintf(stderr, "io_uring_queue_init failed: %s\n", strerror(-ret)); return 1; }

    size_t iterations = TOTAL_SIZE / WRITE_SIZE;
    size_t submitted = 0, completed = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (completed < iterations) {
        // Submit as many writes as the queue allows
        while (submitted - completed < QUEUE_DEPTH && submitted < iterations) {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
            if (!sqe) break;

            off_t offset = submitted * WRITE_SIZE;
            io_uring_prep_write(sqe, fd, buffer, WRITE_SIZE, offset);

            submitted++;
        }

        // Submit all SQEs
        io_uring_submit(&ring);

        // Collect completions
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        io_uring_for_each_cqe(&ring, head, cqe) {
            if (cqe->res < 0) {
                fprintf(stderr, "Write error: %s\n", strerror(-cqe->res));
                return 1;
            }
            count++;
            completed++;
        }
        io_uring_cq_advance(&ring, count);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long long total_ns = ts_to_ns(&end) - ts_to_ns(&start);
    double avg_ms = (double)total_ns / iterations / 1e6;

    // Cast to size_t to match %zu
    printf("io_uring async: wrote %zu MB in %zu ops (%.3f ms avg per op)\n",
           (size_t)(TOTAL_SIZE / (1024*1024)),
           (size_t)iterations,
           avg_ms);

    io_uring_queue_exit(&ring);
    free(buffer);
    close(fd);

    return 0;
}
