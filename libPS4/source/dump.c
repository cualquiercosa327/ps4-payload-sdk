#include "libc.h"
#include "types.h"
#include "memory.h"
#include "dump.h"
#include "elf64.h"

#define TRUE 1
#define FALSE 0

typedef struct {
    int index;
    uint64_t fileoff;
    size_t bufsz;
    size_t filesz;
    int enc;
} SegmentBufInfo;

#define SELF_MAGIC	0x1D3D154F
#define ELF_MAGIC	0x464C457F

int is_self(const char *fn)
{
    struct stat st;
    int res = 0;
    int fd = open(fn, O_RDONLY, 0);
    if (fd != -1) {
        stat(fn, &st);
        void *addr = mmap(0, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (addr != MAP_FAILED) {
            if (st.st_size >= 4)
            {
                uint32_t selfMagic = *(uint32_t*)((uint8_t*)addr + 0x00);
                if (selfMagic == SELF_MAGIC)
                {
                    uint16_t snum = *(uint16_t*)((uint8_t*)addr + 0x18);
                    if (st.st_size >= (0x20 + snum * 0x20 + 4))
                    {
                        uint32_t elfMagic = *(uint32_t*)((uint8_t*)addr + 0x20 + snum * 0x20);
                        if ((selfMagic == SELF_MAGIC) && (elfMagic == ELF_MAGIC))
                            res = 1;
                    }
                }
            }
            munmap(addr, 0x4000);
        }
        else {
        }
        close(fd);
    }
    else {
    }

    return res;
}

#define DECRYPT_SIZE 0x100000

bool read_decrypt_segment(int fd, uint64_t index, uint64_t offset, size_t size, uint8_t *out)
{
    uint8_t *outPtr = out;
    uint64_t outSize = size;
    uint64_t realOffset = (index << 32) | offset;
    while (outSize > 0)
    {
        size_t bytes = (outSize > DECRYPT_SIZE) ? DECRYPT_SIZE : outSize;
        uint8_t *addr = (uint8_t*)mmap(0, bytes, PROT_READ, MAP_PRIVATE | 0x80000, fd, realOffset);
        if (addr != MAP_FAILED)
        {
            memcpy(outPtr, addr, bytes);
            munmap(addr, bytes);
        }
        else
        {
            return FALSE;
        }
        outPtr += bytes;
        outSize -= bytes;
        realOffset += bytes;
    }
    return TRUE;
}

int is_segment_in_other_segment(Elf64_Phdr *phdr, int index, Elf64_Phdr *phdrs, int num) {
    for (int i = 0; i < num; i += 1) {
        Elf64_Phdr *p = &phdrs[i];
        if (i != index) {
            if (p->p_filesz > 0) {
                if ((phdr->p_offset >= p->p_offset) && ((phdr->p_offset + phdr->p_filesz) <= (p->p_offset + p->p_filesz))) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}


SegmentBufInfo *parse_phdr(Elf64_Phdr *phdrs, int num, int *segBufNum) {
    SegmentBufInfo *infos = (SegmentBufInfo *)malloc(sizeof(SegmentBufInfo) * num);
    int segindex = 0;
    for (int i = 0; i < num; i += 1) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_filesz > 0) {
            if ((!is_segment_in_other_segment(phdr, i, phdrs, num)) || (phdr->p_type == 0x6fffff01)) {
                SegmentBufInfo *info = &infos[segindex];
                segindex += 1;
                info->index = i;
                info->bufsz = (phdr->p_filesz + (phdr->p_align - 1)) & (~(phdr->p_align - 1));
                info->filesz = phdr->p_filesz;
                info->fileoff = phdr->p_offset;
                info->enc = (phdr->p_type != 0x6fffff01) ? TRUE : FALSE;
            }
        }
    }
    *segBufNum = segindex;
    return infos;
}

void do_dump(char *saveFile, int fd, SegmentBufInfo *segBufs, int segBufNum, Elf64_Ehdr *ehdr) {
    int sf = open(saveFile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (sf != -1) {
        size_t elfsz = 0x40 + ehdr->e_phnum * sizeof(Elf64_Phdr);
        write(sf, ehdr, elfsz);
        for (int i = 0; i < segBufNum; i += 1) {
			uint8_t *buf = (uint8_t*)malloc(segBufs[i].bufsz);
            memset(buf, 0, segBufs[i].bufsz);
            if (segBufs[i].enc)
            {
                if (read_decrypt_segment(fd, segBufs[i].index, 0, segBufs[i].filesz, buf)) {
                    lseek(sf, segBufs[i].fileoff, SEEK_SET);
                    write(sf, buf, segBufs[i].bufsz);
                }
            }
            else
            {
                lseek(fd, -segBufs[i].filesz, SEEK_END);
                read(fd, buf, segBufs[i].filesz);
                lseek(sf, segBufs[i].fileoff, SEEK_SET);
                write(sf, buf, segBufs[i].filesz);
            }
            free(buf);
        }
        close(sf);
    }
    else {
    }
}

void decrypt_and_dump_self(char *selfFile, char *saveFile) {
    int fd = open(selfFile, O_RDONLY, 0);
    if (fd != -1) {
        void *addr = mmap(0, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (addr != MAP_FAILED) {
            uint16_t snum = *(uint16_t*)((uint8_t*)addr + 0x18);
            Elf64_Ehdr *ehdr = (Elf64_Ehdr *)((uint8_t*)addr + 0x20 + snum * 0x20);
            ehdr->e_shoff = ehdr->e_shentsize = ehdr->e_shnum = ehdr->e_shstrndx = 0;
            Elf64_Phdr *phdrs = (Elf64_Phdr *)((uint8_t *)ehdr + 0x40);
            int segBufNum = 0;
            SegmentBufInfo *segBufs = parse_phdr(phdrs, ehdr->e_phnum, &segBufNum);
            do_dump(saveFile, fd, segBufs, segBufNum, ehdr);
            free(segBufs);
            munmap(addr, 0x4000);
        }
        else {
        }
        close(fd);
    }
    else {
    }
}
