/* Kompilacja: 
$(find /home/phantom/Android/Sdk/ndk -name "armv7a-linux-androideabi30-clang" | head -n 1) \
-O3 -static -march=armv7-a -mfloat-abi=softfp -mfpu=neon -flto voltage_ctrl.c -o voltage_ctrl
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <dirent.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>

#pragma pack(push, 1)
typedef struct {
    pthread_mutex_t lock;      
    uint32_t cpu_temp;         
    uint64_t cpu_freq;         
    uint64_t target_voltage;   
    uint8_t  cmd_type;         
    uint8_t  request_pending;
} shm_bridge_t;
#pragma pack(pop)

int find_memfd_fd_in_proc(int pid, const char* name) {
    char fd_dir_path[64];
    snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);
    DIR* dir = opendir(fd_dir_path);
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char fd_path[128], link_path[256];
        snprintf(fd_path, sizeof(fd_path), "%s/%s", fd_dir_path, entry->d_name);        
        ssize_t len = readlink(fd_path, link_path, sizeof(link_path)-1);
        if (len != -1) {
            link_path[len] = '\0';
            if (strstr(link_path, "memfd:") && strstr(link_path, name)) {
                closedir(dir);
                return atoi(entry->d_name);
            }
        }
    }
    closedir(dir);
    return -1;
}

int get_pid_by_name(const char* name) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (!isdigit(entry->d_name[0])) continue;
        char path[256], cmd[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            memset(cmd, 0, sizeof(cmd));
            int len = read(fd, cmd, sizeof(cmd)-1);
            close(fd);
            if (len > 0 && strstr(cmd, name)) {
                closedir(dir);
                return atoi(entry->d_name);
            }
        }
    }
    closedir(dir);
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <type> <voltage>\n", argv[0]);
        return 1;
    }
    uint8_t type = (uint8_t)atoi(argv[1]);
    uint64_t voltage = strtoull(argv[2], NULL, 10);
    int pid = get_pid_by_name("hw_resident");
    if (pid == -1) {
        fprintf(stderr, "[ERR] Process hw_resident not found\n");
        return 2;
    }
    int remote_fd = find_memfd_fd_in_proc(pid, "perf_bridge");
    if (remote_fd == -1) {
        fprintf(stderr, "[ERR] Memfd 'perf_bridge' not found in PID %d\n", pid);
        return 3;
    }
    char final_path[64];
    snprintf(final_path, sizeof(final_path), "/proc/%d/fd/%d", pid, remote_fd);
    int fd = open(final_path, O_RDWR);
    if (fd < 0) {
        perror("[ERR] Failed to open remote FD");
        return 4;
    }
    shm_bridge_t *bridge = mmap(NULL, sizeof(shm_bridge_t), 
                                   PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (bridge == MAP_FAILED) {
        perror("[ERR] mmap failed");
        return 5;
    }
    pthread_mutex_lock(&bridge->lock);
    bridge->target_voltage = voltage;
    bridge->cmd_type = type;
    __atomic_store_n(&bridge->request_pending, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&bridge->lock);
    printf("[OK] memfd bridge updated: Type %d, Voltage %llu\n", type, (unsigned long long)voltage);
    munmap(bridge, sizeof(shm_bridge_t));
    return 0;
}
