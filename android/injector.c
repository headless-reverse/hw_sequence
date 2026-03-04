/* 
$(find /home/..../Android/Sdk/ndk -name "armv7a-linux-androideabi30-clang" | head -n 1) \
-O3 -pthread -march=armv7-a -mfloat-abi=softfp -mfpu=neon -flto \
injector.c -o injector -llog
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <elf.h>
#include <stdint.h>
#include <stdarg.h>

#define BKPT_ARM 0xe1200070

uintptr_t get_remote_base(int pid, const char *name) {
    char path[256], line[512];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    uintptr_t addr = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name) && strstr(line, "r-xp")) {
            addr = strtoul(line, NULL, 16);
            break;
        }
    }
    fclose(f);
    return addr;
}

uintptr_t find_remote_symbol(int pid, uintptr_t base, const char *sym_name) {
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);
    int fd = open(mem_path, O_RDONLY);
    if (fd < 0) return 0;
    Elf32_Ehdr ehdr;
    pread(fd, &ehdr, sizeof(ehdr), base);
    Elf32_Phdr phdr;
    uintptr_t dyn_addr = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        pread(fd, &phdr, sizeof(phdr), base + ehdr.e_phoff + (i * sizeof(phdr)));
        if (phdr.p_type == PT_DYNAMIC) { dyn_addr = phdr.p_vaddr + base; break; }
    }
    Elf32_Dyn dyn;
    uintptr_t symtab = 0, strtab = 0, hash = 0, gnu_hash = 0;
    for (int i = 0; i < 64; i++) {
        pread(fd, &dyn, sizeof(dyn), dyn_addr + (i * sizeof(dyn)));
        if (dyn.d_tag == DT_SYMTAB) symtab = dyn.d_un.d_ptr + base;
        if (dyn.d_tag == DT_STRTAB) strtab = dyn.d_un.d_ptr + base;
        if (dyn.d_tag == DT_HASH) hash = dyn.d_un.d_ptr + base;
        if (dyn.d_tag == DT_GNU_HASH) gnu_hash = dyn.d_un.d_ptr + base;
        if (dyn.d_tag == DT_NULL) break;
    }
    uint32_t nsyms = 0;
    if (hash) {
        pread(fd, &nsyms, sizeof(uint32_t), hash + 4);
    } else if (gnu_hash) {
        uint32_t nbuckets, symoffset, bloom_size;
        pread(fd, &nbuckets, sizeof(uint32_t), gnu_hash);
        pread(fd, &symoffset, sizeof(uint32_t), gnu_hash + 4);
        pread(fd, &bloom_size, sizeof(uint32_t), gnu_hash + 8);
        uint32_t max_idx = 0;
        for (uint32_t i = 0; i < nbuckets; i++) {
            uint32_t bucket;
            pread(fd, &bucket, sizeof(uint32_t), gnu_hash + 16 + (bloom_size * 4) + (i * 4));
            if (bucket > max_idx) max_idx = bucket;
        }
        nsyms = max_idx + 1024;
    }
    Elf32_Sym sym;
    char name_buf[128];
    for (uint32_t i = 0; i < nsyms; i++) {
        pread(fd, &sym, sizeof(sym), symtab + (i * sizeof(sym)));
        if (sym.st_name != 0) {
            pread(fd, name_buf, sizeof(name_buf), strtab + sym.st_name);
            if (strcmp(name_buf, sym_name) == 0) {
                uintptr_t addr = sym.st_value + base;
                if (ELF32_ST_TYPE(sym.st_info) == STT_FUNC && (addr & 1) == 0) {
                    uint16_t instr;
                    pread(fd, &instr, 2, addr);
                    if ((instr & 0xF800) > 0x4800) addr |= 1;
                }
                close(fd);
                return addr;
            }
        }
    }
    close(fd);
    return 0;
}

int wait_for_target_trap(int pid) {
    int status;
    while (1) {
        if (waitpid(pid, &status, WUNTRACED) <= 0) return -1;
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == SIGTRAP) return 0;
            ptrace(PTRACE_CONT, pid, NULL, (void*)(uintptr_t)sig);
        } else return -1;
    }
}

int ptrace_call(int pid, uintptr_t func_addr, struct pt_regs *regs) {
    regs->uregs[15] = func_addr & ~1;
    if (func_addr & 1) regs->uregs[16] |= 0x20;
    else regs->uregs[16] &= ~0x20;
    ptrace(PTRACE_SETREGS, pid, NULL, regs);
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    if (wait_for_target_trap(pid) == -1) return -1;
    ptrace(PTRACE_GETREGS, pid, NULL, regs);
    return (int)regs->uregs[0];
}

int ptrace_write(int pid, uintptr_t addr, void *data, size_t len) {
    long word;
    for (size_t i = 0; i < len; i += sizeof(long)) {
        word = 0;
        memcpy(&word, (const char*)data + i, (len - i < sizeof(long)) ? len - i : sizeof(long));
        ptrace(PTRACE_POKEDATA, pid, addr + i, (void*)word);
    }
    return 0;
}

int inject_so(int pid, const char *so_path) {
    int *tids = malloc(sizeof(int) * 1024);
    int count = 0;
    char task_path[128];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
    DIR *dir = opendir(task_path);
    struct dirent *ent;
    if (!dir) return -1;
    while ((ent = readdir(dir))) {
        int tid = atoi(ent->d_name);
        if (tid > 0 && ptrace(PTRACE_ATTACH, tid, NULL, NULL) == 0) {
            waitpid(tid, NULL, WUNTRACED);
            tids[count++] = tid;
        }
    }
    closedir(dir);
    struct pt_regs regs, old_regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    old_regs = regs;
    uintptr_t r_libc = get_remote_base(pid, "libc.so");
    uintptr_t r_linker = get_remote_base(pid, "linker");
    uintptr_t p_mmap = find_remote_symbol(pid, r_libc, "mmap");
    uintptr_t p_mprotect = find_remote_symbol(pid, r_libc, "mprotect");
    uintptr_t p_cflush = find_remote_symbol(pid, r_libc, "cacheflush");
    uintptr_t p_dlopen = find_remote_symbol(pid, r_linker, "__loader_dlopen");
    if (!p_dlopen) p_dlopen = find_remote_symbol(pid, r_linker, "dlopen");
    uintptr_t safe_sp = (regs.uregs[13] - 8192) & ~0x7;
    ptrace(PTRACE_POKEDATA, pid, safe_sp, (void*)(uintptr_t)BKPT_ARM);
    regs.uregs[0] = 0; regs.uregs[1] = 8192;
    regs.uregs[2] = PROT_READ | PROT_WRITE;
    regs.uregs[3] = MAP_ANONYMOUS | MAP_PRIVATE;
    unsigned long mmap_args[2] = {(unsigned long)-1, 0};
    ptrace_write(pid, safe_sp, mmap_args, 8);
    regs.uregs[13] = safe_sp; regs.uregs[14] = safe_sp;
    if (ptrace_call(pid, p_mmap, &regs) < 0) goto cleanup;
    uintptr_t map_mem = regs.uregs[0];
    uint32_t shellcode[] = {
        0xf57ff05f, 0xf57ff06f, 0xe92d400f, 0xe1a0000f,
        0xe2800010, 0xe3a01002, 0xe59f2008, 0xe12fff32,
        0xe8bd400f, 0xe1200070, (uint32_t)p_dlopen
    };
    ptrace_write(pid, map_mem, shellcode, sizeof(shellcode));
    ptrace_write(pid, map_mem + sizeof(shellcode), (void*)so_path, strlen(so_path) + 1);
    regs.uregs[0] = map_mem; regs.uregs[1] = 8192;
    regs.uregs[2] = PROT_READ | PROT_EXEC;
    if (ptrace_call(pid, p_mprotect, &regs) < 0) goto cleanup;
    regs.uregs[0] = map_mem; regs.uregs[1] = map_mem + 8192;
    regs.uregs[2] = 0;
    ptrace_call(pid, p_cflush, &regs);
    ptrace_call(pid, map_mem, &regs);
cleanup:
    ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
    for (int i = 0; i < count; i++) ptrace(PTRACE_DETACH, tids[i], NULL, NULL);
    free(tids);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    return inject_so(atoi(argv[1]), argv[2]);
}
