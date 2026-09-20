#include "elf_win32.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(Elf32_Ehdr) == 52, "unexpected ELF32 header layout");
static_assert(sizeof(Elf32_Phdr) == 32, "unexpected ELF32 program header layout");
static_assert(sizeof(Elf32_Shdr) == 40, "unexpected ELF32 section header layout");
static_assert(sizeof(Elf32_Sym) == 16, "unexpected ELF32 symbol layout");
static_assert(sizeof(Elf32_Dyn) == 8, "unexpected ELF32 dynamic entry layout");
static_assert(sizeof(Elf32_Rel) == 8, "unexpected ELF32 REL layout");
static_assert(sizeof(Elf32_Rela) == 12, "unexpected ELF32 RELA layout");
static_assert(offsetof(Elf32_Phdr, p_flags) == 24,
              "unexpected ELF32 program flags offset");

static int check_engine_elf(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "could not open ELF file: %s\n", path);
        return 1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "could not seek ELF file: %s\n", path);
        return 1;
    }
    const long file_size = ftell(file);
    Elf32_Ehdr header{};
    if (file_size < (long)sizeof(header) || fseek(file, 0, SEEK_SET) != 0 ||
        fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        fprintf(stderr, "could not read ELF32 header: %s\n", path);
        return 1;
    }

    const unsigned char magic[] = { 0x7f, 'E', 'L', 'F' };
    const bool valid_header =
        memcmp(header.e_ident, magic, sizeof(magic)) == 0 &&
        header.e_ident[EI_CLASS] == ELFCLASS32 &&
        header.e_ident[EI_DATA] == ELFDATA2LSB &&
        header.e_type == ET_DYN && header.e_machine == EM_386 &&
        header.e_ehsize == sizeof(Elf32_Ehdr) &&
        header.e_phentsize == sizeof(Elf32_Phdr) && header.e_phnum > 0 &&
        (!header.e_shnum || header.e_shentsize == sizeof(Elf32_Shdr));
    const uint64_t program_end = (uint64_t)header.e_phoff +
        (uint64_t)header.e_phnum * header.e_phentsize;
    const uint64_t section_end = (uint64_t)header.e_shoff +
        (uint64_t)header.e_shnum * header.e_shentsize;
    if (!valid_header || program_end > (uint64_t)file_size ||
        (header.e_shnum && section_end > (uint64_t)file_size) ||
        fseek(file, (long)header.e_phoff, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "invalid ELF32 i386 header or table bounds: %s\n", path);
        return 1;
    }

    unsigned int load_segments = 0;
    for (unsigned int i = 0; i < header.e_phnum; ++i) {
        Elf32_Phdr program{};
        if (fread(&program, sizeof(program), 1, file) != 1) {
            fclose(file);
            fprintf(stderr, "could not read ELF program headers: %s\n", path);
            return 1;
        }
        if (program.p_type == PT_LOAD) {
            ++load_segments;
            if (program.p_filesz > program.p_memsz ||
                (uint64_t)program.p_offset + program.p_filesz >
                    (uint64_t)file_size) {
                fclose(file);
                fprintf(stderr, "invalid PT_LOAD segment bounds: %s\n", path);
                return 1;
            }
        }
    }
    fclose(file);

    if (!load_segments) {
        fprintf(stderr, "ELF file has no PT_LOAD segments: %s\n", path);
        return 1;
    }

    printf("parsed ELF32 i386 image (%u program headers, %u PT_LOAD): %s\n",
           (unsigned int)header.e_phnum, load_segments, path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        return check_engine_elf(argv[1]);
    return 0;
}
