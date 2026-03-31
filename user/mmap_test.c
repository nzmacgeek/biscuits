#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    printf("mmap test start\n");

    // Anonymous mmap
    size_t sz = 8192;
    void *a = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (a == MAP_FAILED) { perror("mmap anon"); return 1; }
    strcpy((char*)a, "hello anon");
    printf("anon: %s\n", (char*)a);

    // mprotect: remove write
    if (mprotect(a, sz, PROT_READ) != 0) perror("mprotect");
    // attempt to write (may segfault if protections enforced)
    // ((char*)a)[0] = 'H'; // skip writing to be safe

    // munmap
    if (munmap(a, sz) != 0) perror("munmap");
    printf("anon unmapped\n");

    // Create a small file and write data
    const char *path = "/tmp/mmap_test_data";
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) { perror("open file"); return 1; }
    const char *payload = "This is file-backed mmap data\n";
    write(fd, payload, strlen(payload));
    lseek(fd, 0, SEEK_SET);

    // file-backed mmap
    void *f = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (f == MAP_FAILED) { perror("mmap file"); close(fd); return 1; }
    printf("file: %s\n", (char*)f);

    if (munmap(f, 4096) != 0) perror("munmap file");
    close(fd);

    printf("mmap test end\n");
    return 0;
}
