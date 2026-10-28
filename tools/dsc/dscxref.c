/*
 * dscxref: in an iOS split dyld shared cache, turn (subcache file, file offset) into a VM address,
 * and find the ADRP+ADD / ADRP+LDR pairs that build a given address (code xrefs).
 *
 *   dscxref va  FILE OFFSET            print the VM address of FILE's byte at OFFSET
 *   dscxref off VA FILE...             print which FILE and offset hold VA
 *   dscxref xref VA FILE...            scan FILE's executable mappings for code making VA
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct mapping { uint64_t address, size, fileOffset; uint32_t maxProt, initProt; };

static const uint8_t *
map_file(const char *path, size_t *len)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0) {
		perror(path);
		exit(1);
	}
	*len = (size_t)st.st_size;
	void *p = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	close(fd);
	return p;
}

static unsigned
mappings(const uint8_t *f, struct mapping **out)
{
	if (memcmp(f, "dyld_v1", 7) != 0) {
		return 0;
	}
	uint32_t off, count;
	memcpy(&off, f + 0x10, 4);
	memcpy(&count, f + 0x14, 4);
	*out = (struct mapping *)(f + off);
	return count;
}

int
main(int argc, char **argv)
{
	if (argc >= 4 && strcmp(argv[1], "va") == 0) {
		size_t len;
		const uint8_t *f = map_file(argv[2], &len);
		uint64_t o = strtoull(argv[3], NULL, 0);
		struct mapping *m;
		unsigned n = mappings(f, &m);
		for (unsigned i = 0; i < n; i++) {
			if (o >= m[i].fileOffset && o < m[i].fileOffset + m[i].size) {
				printf("0x%llx\n", (unsigned long long)(m[i].address + o - m[i].fileOffset));
				return 0;
			}
		}
		fprintf(stderr, "offset not in a mapping\n");
		return 1;
	}
	if (argc >= 4 && (strcmp(argv[1], "xref") == 0 || strcmp(argv[1], "off") == 0)) {
		uint64_t target = strtoull(argv[2], NULL, 0);
		bool xref = strcmp(argv[1], "xref") == 0;
		for (int a = 3; a < argc; a++) {
			size_t len;
			const uint8_t *f = map_file(argv[a], &len);
			struct mapping *m;
			unsigned n = mappings(f, &m);
			for (unsigned i = 0; i < n; i++) {
				if (!xref) {
					if (target >= m[i].address && target < m[i].address + m[i].size) {
						printf("%s 0x%llx\n", argv[a], (unsigned long long)(target - m[i].address + m[i].fileOffset));
					}
					continue;
				}
				if (!(m[i].initProt & 4) || m[i].fileOffset + m[i].size > len) {
					continue;
				}
				const uint32_t *w = (const uint32_t *)(f + m[i].fileOffset);
				size_t words = m[i].size / 4;
				for (size_t k = 0; k + 1 < words; k++) {
					uint32_t ins = w[k];
					if ((ins & 0x9f000000u) != 0x90000000u) {
						continue;               /* ADRP */
					}
					uint64_t pc = m[i].address + 4 * k;
					int64_t imm = (int64_t)((((ins >> 5) & 0x7ffffu) << 2) | ((ins >> 29) & 3u));
					if (imm & (1 << 20)) {
						imm -= 1 << 21;
					}
					uint64_t page = (pc & ~0xfffULL) + (uint64_t)(imm << 12);
					if (page != (target & ~0xfffULL)) {
						continue;
					}
					unsigned rd = ins & 31;
					for (size_t j = k + 1; j < words && j < k + 8; j++) {
						uint32_t n2 = w[j];
						uint64_t lo = target & 0xfff;
						bool add = (n2 & 0xffc00000u) == 0x91000000u && ((n2 >> 5) & 31) == rd &&
						    ((n2 >> 10) & 0xfff) == lo;
						bool ldr = (n2 & 0xffc00000u) == 0xf9400000u && ((n2 >> 5) & 31) == rd &&
						    ((n2 >> 10) & 0xfff) * 8 == lo;
						bool ldrsw = (n2 & 0xffc00000u) == 0xb9800000u && ((n2 >> 5) & 31) == rd &&
						    ((n2 >> 10) & 0xfff) * 4 == lo;
						if (add || ldr || ldrsw) {
							printf("%s 0x%llx (file 0x%llx) %s\n", argv[a], (unsigned long long)pc,
							    (unsigned long long)(m[i].fileOffset + 4 * k), add ? "adrp+add" : ldr ? "adrp+ldr" : "adrp+ldrsw");
							break;
						}
					}
				}
			}
		}
		return 0;
	}
	fprintf(stderr, "usage: see the comment at the top\n");
	return 2;
}
