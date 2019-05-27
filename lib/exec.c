#include <inc/lib.h>
#include <inc/elf.h>

#define TEMP2USTACK(addr) ((uintptr_t)((void*) (addr) + ((void *)USTACKTOP - *nextpgp)))

static int init_stack(const char **argv, uintptr_t *init_esp, void **nextpgp);
static int map_segment(uintptr_t va, size_t memsz, int fd, size_t filesz,
    off_t fileoffset, int perm, struct SegmentInfo *seginfo, void **nextpgp);

int
exec(const char *prog, const char **argv)
{
    extern char end[];
    unsigned char elf_buf[512];
    struct Trapframe tf;

    int fd, i, r, perm;
    struct Elf *elf;
    struct Proghdr *ph;

    cprintf("1 %p\n", argv);

    struct SegmentInfo *seginfo = (struct SegmentInfo *) UTEMP;
    void *nextpg = ROUNDUP((void *)end, PGSIZE);

    // Open ELF file
    if ((r = open(prog, O_RDONLY)) < 0)
        return r;
    fd = r;

    // Read ELF header
    elf = (struct Elf*) elf_buf;
    if (readn(fd, elf_buf, sizeof(elf_buf)) != sizeof(elf_buf)
        || elf->e_magic != ELF_MAGIC) {
        close(fd);
        cprintf("elf magic %08x want %08x\n", elf->e_magic, ELF_MAGIC);
        return -E_NOT_EXEC;
    }

    // alloc a page for 'seginfo'
    if ((r = sys_page_alloc(0, seginfo, PTE_W | PTE_U | PTE_P)) < 0)
        return r;

    // Set up tf, including initial stack.
    tf = envs[ENVX(sys_getenvid())].env_tf;
    tf.tf_eip = elf->e_entry;

    seginfo->srcva = nextpg;
    seginfo->dstva = (void *)USTACKTOP - PGSIZE;
    seginfo->size = PGSIZE;
    seginfo->perm = PTE_W | PTE_U | PTE_P;
    seginfo++;

    cprintf("0 %p\n", argv);
    if (!argv)
        return -E_INVAL;

    if ((r = init_stack(argv, &tf.tf_esp, &nextpg)) < 0)
        goto error;

    // Set up program segments as defined in ELF header.
    ph = (struct Proghdr*) (elf_buf + elf->e_phoff);
    for (i = 0; i < elf->e_phnum; i++, ph++) {
        if (ph->p_type != ELF_PROG_LOAD)
            continue;
        perm = PTE_P | PTE_U;
        if (ph->p_flags & ELF_PROG_FLAG_WRITE)
            perm |= PTE_W;
        if ((r = map_segment(ph->p_va, ph->p_memsz, fd, ph->p_filesz,
                ph->p_offset, perm, seginfo, &nextpg)) < 0)
            goto error;
        seginfo++;
    }
    close(fd);

    if ((r = sys_env_load_elf(&tf, (struct SegmentInfo *)UTEMP)) < 0)
        goto error;

    return -1;

error:
    for (nextpg -= PGSIZE; nextpg >= (void *)end; nextpg -= PGSIZE)
        sys_page_unmap(0, nextpg);
    sys_page_unmap(0, (void *)UTEMP);
    return r;
}

static int
init_stack(const char **argv, uintptr_t *init_esp, void **nextpgp)
{
    size_t string_size;
    int argc, i, r;
    char *string_store;
    uintptr_t *argv_store;

    string_size = 0;
    for (argc = 0; argv[argc] != 0; argc++)
        string_size += strlen(argv[argc]) + 1;

    string_store = (char*) *nextpgp + PGSIZE - string_size - 4; // -4 to work with lab4 sfork's thisenv setting
    argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));

    if ((void*) (argv_store - 2) < (void*) *nextpgp)
        return -E_NO_MEM;

    if ((r = sys_page_alloc(0, (void*) *nextpgp, PTE_P | PTE_U | PTE_W)) < 0)
        return r;

    for (i = 0; i < argc; i++) {
        argv_store[i] = TEMP2USTACK(string_store);
        strcpy(string_store, argv[i]);
        string_store += strlen(argv[i]) + 1;
    }
    argv_store[argc] = 0;

    *nextpgp += PGSIZE;
    assert(string_store == (char*)*nextpgp - 4); // -4 to work with lab4 sfork's thisenv setting

    argv_store[-1] = TEMP2USTACK(argv_store);
    argv_store[-2] = argc;

    *init_esp = TEMP2USTACK(&argv_store[-2]);

    return 0;
}

static int
map_segment(uintptr_t va, size_t memsz, int fd, size_t filesz,
    off_t fileoffset, int perm, struct SegmentInfo *seginfo, void **nextpgp)
{
    int i, r;
    void *blk;

    if ((i = PGOFF(va))) {
        va -= i;
        memsz += i;
        filesz += i;
        fileoffset -= i;
    }

    seginfo->dstva = (void *)va;
    seginfo->srcva = *nextpgp;
    seginfo->size = memsz;
    seginfo->perm = perm;

    for (i = 0; i < memsz; i += PGSIZE) {
        if (i >= filesz) {
            // allocate a blank page
            if ((r = sys_page_alloc(0, *nextpgp, perm)) < 0)
                return r;
            *nextpgp += PGSIZE;
        } else {
            // from file
            if ((r = sys_page_alloc(0, *nextpgp, PTE_P | PTE_U | PTE_W)) < 0)
                return r;
            *nextpgp += PGSIZE;
            if ((r = seek(fd, fileoffset + i)) < 0)
                return r;
            if ((r = readn(fd, *nextpgp - PGSIZE, MIN(PGSIZE, filesz - i))) < 0)
                return r;
        }
    }
    return 0;
}
