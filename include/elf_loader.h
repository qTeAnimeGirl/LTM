#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "ltm.h"

/* ELF64 standard data types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#define EI_NIDENT 16

/* ELF64 Header */
typedef struct {
    unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
    Elf64_Half    e_type;             /* Object file type */
    Elf64_Half    e_machine;          /* Architecture */
    Elf64_Word    e_version;          /* Object file version */
    Elf64_Addr    e_entry;            /* Entry point virtual address */
    Elf64_Off     e_phoff;            /* Program header table file offset */
    Elf64_Off     e_shoff;            /* Section header table file offset */
    Elf64_Word    e_flags;            /* Processor-specific flags */
    Elf64_Half    e_ehsize;           /* ELF header size in bytes */
    Elf64_Half    e_phentsize;        /* Program header table entry size */
    Elf64_Half    e_phnum;            /* Program header table entry count */
    Elf64_Half    e_shentsize;        /* Section header table entry size */
    Elf64_Half    e_shnum;            /* Section header table entry count */
    Elf64_Half    e_shstrndx;         /* Section header string table index */
} Elf64_Ehdr;

/* ELF64 Program Header */
typedef struct {
    Elf64_Word  p_type;    /* Segment type */
    Elf64_Word  p_flags;   /* Segment flags */
    Elf64_Off   p_offset;  /* Segment file offset */
    Elf64_Addr  p_vaddr;   /* Segment virtual address */
    Elf64_Addr  p_paddr;   /* Segment physical address */
    Elf64_Xword p_filesz;  /* Segment size in file */
    Elf64_Xword p_memsz;   /* Segment size in memory */
    Elf64_Xword p_align;   /* Segment alignment */
} Elf64_Phdr;

/* e_ident indexes */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8

/* Identification values */
#define ELFMAG0       0x7f
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'

#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define EV_CURRENT    1

/* e_type values */
#define ET_NONE       0
#define ET_REL        1
#define ET_EXEC       2
#define ET_DYN        3
#define ET_CORE       4

/* e_machine values */
#define EM_AARCH64    183

/* Program header types */
#define PT_NULL       0
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_INTERP     3
#define PT_NOTE       4
#define PT_SHLIB      5
#define PT_PHDR       6
#define PT_TLS        7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

/* Program header flags */
#define PF_X          0x1
#define PF_W          0x2
#define PF_R          0x4

/* Dynamic Section & Relocations */
typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr  d_ptr;
    } d_un;
} Elf64_Dyn;

#define DT_NULL       0
#define DT_RELA       7
#define DT_RELASZ     8
#define DT_RELAENT    9

#define R_AARCH64_RELATIVE 1027

/* Function prototypes */
int elf_load_binary(const char *filepath, ltm_runtime_t *rt);
int elf_setup_stack(ltm_runtime_t *rt);
void ltm_jump_to_entry(uintptr_t entry, void *sp) __attribute__((noreturn));
void ltm_resume_context(void *state, void *neon) __attribute__((noreturn));

#endif /* ELF_LOADER_H */
