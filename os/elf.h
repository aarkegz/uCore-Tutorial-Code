#ifndef ELF_H
#define ELF_H

#include "types.h"

#define EI_NIDENT 16

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_RISCV 243

#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
	uint8 e_ident[EI_NIDENT];
	uint16 e_type;
	uint16 e_machine;
	uint32 e_version;
	uint64 e_entry;
	uint64 e_phoff;
	uint64 e_shoff;
	uint32 e_flags;
	uint16 e_ehsize;
	uint16 e_phentsize;
	uint16 e_phnum;
	uint16 e_shentsize;
	uint16 e_shnum;
	uint16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	uint32 p_type;
	uint32 p_flags;
	uint64 p_offset;
	uint64 p_vaddr;
	uint64 p_paddr;
	uint64 p_filesz;
	uint64 p_memsz;
	uint64 p_align;
} Elf64_Phdr;

static inline int is_elf(uint64 start)
{
	uint8 *magic = (uint8 *)start;
	return magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
	       magic[2] == ELFMAG2 && magic[3] == ELFMAG3;
}

#endif // ELF_H
