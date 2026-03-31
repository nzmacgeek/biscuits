#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define SWAP_MAGIC       0x53574150u
#define SWAP_PAGE_SIZE   4096u
#define HOST_SECTOR_SIZE 512u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t total_pages;
    uint32_t used_pages;
    uint8_t  label[16];
    uint8_t  reserved[4060];
} swap_header_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-L label] [-o start-sector] [-n sectors] <image>\n",
            prog);
}

int main(int argc, char *argv[]) {
    const char *target = NULL;
    const char *label = "ChatterSwap";
    uint32_t offset_sectors = 0;
    uint32_t region_sectors = 0;
    FILE *fp;
    swap_header_t hdr;
    long offset;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            offset_sectors = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            region_sectors = (uint32_t)atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            target = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!target || region_sectors < (SWAP_PAGE_SIZE / HOST_SECTOR_SIZE) * 2u) {
        usage(argv[0]);
        return 1;
    }

    fp = fopen(target, "r+b");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SWAP_MAGIC;
    hdr.version = 1;
    hdr.total_pages = region_sectors / (SWAP_PAGE_SIZE / HOST_SECTOR_SIZE);
    if (hdr.total_pages > 0) hdr.total_pages -= 1u;
    strncpy((char *)hdr.label, label, sizeof(hdr.label) - 1u);

    offset = (long)((uint64_t)offset_sectors * HOST_SECTOR_SIZE);
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("fseek");
        fclose(fp);
        return 1;
    }

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        perror("fwrite");
        fclose(fp);
        return 1;
    }

    fclose(fp);
    printf("[MKSWAP] Bandit's storage shed is ready: %u pages at sector %u\n",
           hdr.total_pages, offset_sectors);
    return 0;
}