#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <stdbool.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <linux/ashmem.h>
#include <inttypes.h>

#pragma pack(push, 1)
typedef struct {
    pthread_mutex_t lock;      
    uint32_t cpu_temp;         
    uint64_t cpu_freq;         
    uint64_t target_voltage;   
    uint8_t  cmd_type;         
    uint8_t  request_pending;  
} ashmem_bridge_t;
#pragma pack(pop)

ashmem_bridge_t *global_bridge = NULL;

#define CONTROL_PORT 22222
#define ALLOWED_IP "192.168.0.10"
#define CONTROL_MAGIC 0x41444253
#define PROTOCOL_HEAD 0x55
#define MAX_FDS 64
#define DEV_NAME "🚷hw_resident"

#define EVENT_TYPE_KEY              1
#define EVENT_TYPE_TOUCH_DOWN       2
#define EVENT_TYPE_TOUCH_UP         3
#define EVENT_TYPE_TOUCH_MOVE       4
#define EVENT_TYPE_SCROLL           5
#define EVENT_TYPE_BACK				6
#define EVENT_TYPE_HOME				7
#define EVENT_TYPE_ADB_WIFI         20
#define EVENT_TYPE_GRAB_TOUCH       21
#define EVENT_TYPE_GRAB_KEYS        22
#define EVENT_TYPE_GET_PROCS        23
#define EVENT_TYPE_INJECT_PID       24
#define EVENT_TYPE_GET_MEM_MAPS     25
#define EVENT_TYPE_READ_MEM         26
#define EVENT_TYPE_KILL_PROC        27
#define EVENT_TYPE_PROC_STATUS      28
#define EVENT_TYPE_CHECK_LIB        29
#define EVENT_TYPE_SET_LIB_PATH     30
#define EVENT_TYPE_SCAN_LIBS        31
#define EVENT_TYPE_PANIC_RESET      99

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

#pragma pack(push, 1)
struct ControlPacket {
    uint8_t head;
    uint32_t magic;
    uint8_t type;
    uint16_t x;
    uint16_t y;
    uint16_t data;
    uint8_t crc;
} __attribute__((packed));
#pragma pack(pop)

static char current_so_path[512] = "/system/lib/resident_lib.so";

int physical_fds[MAX_FDS];
int physical_count = 0;
int touch_fds[8];
int touch_count = 0;
int key_fds[8];
int key_count = 0;
int uinput_fd = -1;
static int trace_fd = -1;
static int global_tracking_id_counter = 100;
static int slot_active[10] = {0};
static int active_slots = 0;
static uint16_t last_mt_x[10] = {0};
static uint16_t last_mt_y[10] = {0};

void sys_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[hw_resident] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static inline ssize_t raw_write(int fd, const void *buf, size_t count) {return syscall(__NR_write, fd, buf, count);}

void mark_trace(const char *msg) {if (trace_fd >= 0) raw_write(trace_fd, msg, strlen(msg));}

void setup_rt_priority() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
    struct sched_param param;
    param.sched_priority = 95;
    sched_setscheduler(0, SCHED_FIFO, &param);
    mlockall(MCL_CURRENT | MCL_FUTURE);
    sys_log("RT (CPU3) Priority Engaged.");
}

uint8_t calculate_crc(const struct ControlPacket* pkt) {
    size_t data_len = offsetof(struct ControlPacket, crc) - offsetof(struct ControlPacket, magic);
    const uint8_t *data_ptr = (const uint8_t*)&pkt->magic;
    uint8_t crc = 0;
    for (size_t i = 0; i < data_len; i++) { crc ^= data_ptr[i]; }
    return crc;
}

void emit(int type, int code, int val) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = val;
    raw_write(uinput_fd, &ie, sizeof(ie));
}

void panic_reset() {
    for (int i = 0; i < 10; i++) {
        emit(EV_ABS, ABS_MT_SLOT, i);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        slot_active[i] = 0;
    }
    active_slots = 0;
    emit(EV_KEY, BTN_TOUCH, 0);
    emit(EV_SYN, SYN_REPORT, 0);
    sys_log("PANIC RESET: All slots cleared.");
}

void handle_mt_logic(struct ControlPacket* pkt) {
    uint16_t slot = ntohs(pkt->data) % 10;
    uint16_t x = ntohs(pkt->x);
    uint16_t y = ntohs(pkt->y);
    emit(EV_ABS, ABS_MT_SLOT, slot);
    if (pkt->type == EVENT_TYPE_TOUCH_DOWN) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, global_tracking_id_counter++);
        if (global_tracking_id_counter > 65000) global_tracking_id_counter = 100;
        emit(EV_ABS, ABS_MT_POSITION_X, x);
        emit(EV_ABS, ABS_MT_POSITION_Y, y);
        emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 6);
        emit(EV_ABS, ABS_MT_PRESSURE, 60);
        last_mt_x[slot] = x;
		last_mt_y[slot] = y;
        if (!slot_active[slot]) { slot_active[slot] = 1; active_slots++; }
        if (active_slots == 1) emit(EV_KEY, BTN_TOUCH, 1);
    } 
    else if (pkt->type == EVENT_TYPE_TOUCH_MOVE) {
        if (slot_active[slot] && (abs(x - last_mt_x[slot]) > 1 || abs(y - last_mt_y[slot]) > 1)) {
            emit(EV_ABS, ABS_MT_POSITION_X, x);
            emit(EV_ABS, ABS_MT_POSITION_Y, y);
            last_mt_x[slot] = x;
            last_mt_y[slot] = y;
        } else return;
    } 
    else if (pkt->type == EVENT_TYPE_TOUCH_UP) {
        if (slot_active[slot]) {
            emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            slot_active[slot] = 0;
            active_slots--;
            if (active_slots < 0) active_slots = 0;
        }
        if (active_slots == 0) emit(EV_KEY, BTN_TOUCH, 0);
    }
    emit(EV_SYN, SYN_REPORT, 0);
}

void send_to_client(int client_fd, uint8_t type, uint16_t x, uint16_t y, uint16_t data) {
    if (client_fd < 0) return;
    struct ControlPacket pkt = {0};
    pkt.head = PROTOCOL_HEAD;
    pkt.magic = htonl(CONTROL_MAGIC);
    pkt.type = type;
    pkt.x = htons(x);
    pkt.y = htons(y);
    pkt.data = htons(data);
    pkt.crc = calculate_crc(&pkt);
    raw_write(client_fd, &pkt, sizeof(pkt));
}

void send_proc_entry(int client_fd, int pid, const char* name) {
    int len = strlen(name);
    for (int i = 0; i < len; i += 4) {
        uint16_t x = 0, y = 0;
        memcpy(&x, name + i, (len - i >= 2) ? 2 : (len - i));
        if (len - i > 2) {
            memcpy(&y, name + i + 2, (len - i >= 4) ? 2 : (len - i - 2));
        }
        send_to_client(client_fd, EVENT_TYPE_GET_PROCS, x, y, (uint16_t)pid);
    }
    send_to_client(client_fd, EVENT_TYPE_GET_PROCS, 0, 0, (uint16_t)pid);
}

void process_packet(int client_fd, struct ControlPacket* pkt) {
    mark_trace("INJECT_START");
    if (pkt->head != PROTOCOL_HEAD) return;
    uint16_t d = ntohs(pkt->data);
    switch (pkt->type) {
        case EVENT_TYPE_TOUCH_DOWN:
        case EVENT_TYPE_TOUCH_MOVE:
        case EVENT_TYPE_TOUCH_UP: 
            handle_mt_logic(pkt); 
            break;
        case EVENT_TYPE_KEY:      
            emit(EV_KEY, d, ntohs(pkt->x)); 
            emit(EV_SYN, SYN_REPORT, 0); 
            break;
        case EVENT_TYPE_GRAB_TOUCH: 
            for (int i = 0; i < touch_count; i++) 
                ioctl(touch_fds[i], EVIOCGRAB, (unsigned long)d);
            sys_log(d ? "Touch Exclusive: ON" : "Touch Exclusive: OFF");
            break;
        case EVENT_TYPE_GRAB_KEYS:
            for (int i = 0; i < key_count; i++) 
                ioctl(key_fds[i], EVIOCGRAB, (unsigned long)d);
            sys_log(d ? "Keys Exclusive: ON" : "Keys Exclusive: OFF");
            break;
        case EVENT_TYPE_BACK:     
            emit(EV_KEY, KEY_BACK, 1); emit(EV_SYN, SYN_REPORT, 0); 
            emit(EV_KEY, KEY_BACK, 0); emit(EV_SYN, SYN_REPORT, 0); 
            break;
        case EVENT_TYPE_HOME:     
            emit(EV_KEY, KEY_HOMEPAGE, 1); emit(EV_SYN, SYN_REPORT, 0); 
            emit(EV_KEY, KEY_HOMEPAGE, 0); emit(EV_SYN, SYN_REPORT, 0); 
            break;
        case EVENT_TYPE_GET_PROCS: {
            DIR *dir = opendir("/proc");
            if (dir) {
                struct dirent *ent;
                while ((ent = readdir(dir))) {
                    if (!isdigit(ent->d_name[0])) continue;
                    char path[256], cmdline[512];
                    snprintf(path, sizeof(path), "/proc/%s/cmdline", ent->d_name);
                    int fd = open(path, O_RDONLY);
                    if (fd >= 0) {
                        memset(cmdline, 0, sizeof(cmdline));
                        int len = read(fd, cmdline, sizeof(cmdline) - 1);
                        close(fd);
                        if (len > 0) send_proc_entry(client_fd, atoi(ent->d_name), cmdline);
                    }
                }
                closedir(dir);
            }
            send_to_client(client_fd, EVENT_TYPE_GET_PROCS, 0, 0, 0xFFFF);
            break;
		}
		case EVENT_TYPE_SET_LIB_PATH: {
            uint16_t path_len = ntohs(pkt->x);
            if (path_len > 0 && path_len < (sizeof(current_so_path) - 1)) {
                memset(current_so_path, 0, sizeof(current_so_path));
                int r = recv(client_fd, current_so_path, path_len, MSG_WAITALL);
                if (r > 0) {
                    current_so_path[r] = '\0';
                    for(int i = 0; i < r; i++) {
                        if(current_so_path[i] == '\r' || current_so_path[i] == '\n') 
                            current_so_path[i] = '\0';
                    }
                    sys_log("PATH_VALIDATED: [%s]", current_so_path);
                }
            }
            break;
        }
		case EVENT_TYPE_INJECT_PID: {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "/system/bin/injector %d %s", (int)d, current_so_path);
            sys_log("Executing: %s", cmd);
            int res = system(cmd);
            int exit_code = WEXITSTATUS(res);
            send_to_client(client_fd, EVENT_TYPE_INJECT_PID, (exit_code == 0 ? 1 : 0), 0, (uint16_t)d);
            sys_log("Injection result for PID %d: %s", (int)d, exit_code == 0 ? "SUCCESS" : "FAILED");
            break;
        }
		case EVENT_TYPE_GET_MEM_MAPS: {
			char path[64];
			snprintf(path, sizeof(path), "/proc/%d/maps", (int)d);
			FILE* f = fopen(path, "r");
			if (f) {
				char line[512];
				while (fgets(line, sizeof(line), f)) {
					int len = strlen(line);
					for (int i = 0; i < len; i += 4) {
						uint16_t x = 0, y = 0;
						memcpy(&x, line + i, (len - i >= 2) ? 2 : (len - i));
						if (len - i > 2) {
							memcpy(&y, line + i + 2, (len - i >= 4) ? 2 : (len - i - 2));
						}
						send_to_client(client_fd, EVENT_TYPE_GET_MEM_MAPS, x, y, (uint16_t)d);
					}
				}
				fclose(f);
			}
			send_to_client(client_fd, EVENT_TYPE_GET_MEM_MAPS, 0xFFFF, 0xFFFF, (uint16_t)d);
			break;
		}
        case EVENT_TYPE_KILL_PROC:
            kill((int)d, SIGKILL);
            sys_log("Killed PID %d", (int)d);
            break;
        case EVENT_TYPE_READ_MEM:
            sys_log("ReadMem request for PID %d (not implemented)", (int)d);
            break;
        case EVENT_TYPE_PROC_STATUS:
            sys_log("Status request for PID %d", (int)d);
            break;
        case EVENT_TYPE_CHECK_LIB:
            sys_log("Checking lib in PID %d", (int)d);
            break;
        case EVENT_TYPE_SCAN_LIBS:
            sys_log("Scanning libs in PID %d", (int)d);
            break;
        case EVENT_TYPE_ADB_WIFI:
            __system_property_set("service.adb.tcp.port", "1337");
            __system_property_set("ctl.restart", "adbd");
            break;
        case EVENT_TYPE_PANIC_RESET:
            panic_reset();
            break;
        default:
            sys_log("Unknown event: %d", pkt->type);
            break;
    }
    mark_trace("INJECT_DONE");
}

void sys_write(const char* path, const char* val) {
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, val, strlen(val));
        close(fd);
    }
}

void set_regulator_voltage_raw(const char* target_name, uint64_t microvolts) {
    char uv_str[32];
    snprintf(uv_str, sizeof(uv_str), "%" PRIu64, microvolts);
    const char* base_path = "/sys/class/regulator";
    DIR *dir = opendir(base_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        char name_path[256], volt_path[256], name[128];
        snprintf(name_path, sizeof(name_path), "%s/%s/name", base_path, entry->d_name);
        int fd = open(name_path, O_RDONLY);
        if (fd >= 0) {
            int len = read(fd, name, sizeof(name)-1);
            if (len > 0) {
                name[len-1] = '\0';
                if (strstr(name, target_name)) {
                    snprintf(volt_path, sizeof(volt_path), "%s/%s/min_microvolts", base_path, entry->d_name);
                    sys_write(volt_path, uv_str);
                    snprintf(volt_path, sizeof(volt_path), "%s/%s/max_microvolts", base_path, entry->d_name);
                    sys_write(volt_path, uv_str);
                }
            }
            close(fd);
        }
    }
    closedir(dir);
}

void init_ashmem_bridge() {
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0) {
        sys_log("Critical: Failed to open ashmem");
        return;
    }
    ioctl(fd, ASHMEM_SET_NAME, "perf_bridge");
    ioctl(fd, ASHMEM_SET_SIZE, sizeof(ashmem_bridge_t));
    global_bridge = mmap(NULL, sizeof(ashmem_bridge_t), 
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (global_bridge == MAP_FAILED) {
        sys_log("Critical: mmap failed");
        close(fd);
        return;
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&global_bridge->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    __atomic_store_n(&global_bridge->request_pending, 0, __ATOMIC_RELEASE);
    sys_log("Ashmem Bridge Ready. FD: %d", fd);
}

void check_ashmem_commands() {
    if (!global_bridge) return;
    if (__atomic_load_n(&global_bridge->request_pending, __ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&global_bridge->lock);
        uint8_t type = global_bridge->cmd_type;
        uint64_t v = global_bridge->target_voltage;
        __atomic_store_n(&global_bridge->request_pending, 0, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&global_bridge->lock);
        if (type == 0x01) set_regulator_voltage_raw("cpu", v);
        else if (type == 0x02) set_regulator_voltage_raw("gpu", v);
    }
}

void run_daemon_loop(int srv_fd) {
    struct pollfd fds[MAX_FDS + 2];
    int client_fd = -1;
    static int last_x[10], last_y[10], phys_slot_active[10], has_coords[10];
    static int current_slot = 0;
    static struct timeval last_send[10] = {0};
    while (1) {
        int poll_count = 0;
        fds[poll_count].fd = srv_fd;
        fds[poll_count].events = POLLIN;
        int srv_idx = poll_count++;
        int cli_idx = -1;
        if (client_fd >= 0) {
            fds[poll_count].fd = client_fd;
            fds[poll_count].events = POLLIN;
            cli_idx = poll_count++;
            for (int i = 0; i < physical_count; i++) {
                fds[poll_count].fd = physical_fds[i];
                fds[poll_count].events = POLLIN;
                poll_count++;
            }
        }
        if (poll(fds, poll_count, -1) <= 0) continue;
        if (fds[srv_idx].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int n = accept(srv_fd, (struct sockaddr*)&client_addr, &addr_len);
            
            if (n >= 0) {
                char *client_ip = inet_ntoa(client_addr.sin_addr);
                if (strcmp(client_ip, ALLOWED_IP) != 0) {
                    sys_log("BLOCKED: Unauthorized connection attempt from %s", client_ip);
                    close(n); 
                } else {
                    if (client_fd >= 0) close(client_fd);
                    client_fd = n;
                    int opt = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
                    sys_log("Client connected: %s", client_ip);
                }
            }
        }
        if (cli_idx != -1 && (fds[cli_idx].revents & POLLIN)) {
            struct ControlPacket pkt;
            uint8_t *buf = (uint8_t*)&pkt;
            ssize_t total = 0;
            while (total < 13) {
                ssize_t r = read(client_fd, buf + total, 13 - total);
                if (r <= 0) break;
                total += r;
            }
            if (total == 13 && ntohl(pkt.magic) == CONTROL_MAGIC && calculate_crc(&pkt) == pkt.crc) {
                process_packet(client_fd, &pkt);
            } else if (total <= 0) {
                close(client_fd); client_fd = -1;
                sys_log("Client disconnected.");
            }
        }
        if (client_fd >= 0 && cli_idx != -1) {
            for (int i = 0; i < physical_count; i++) {
                if (fds[cli_idx + 1 + i].revents & POLLIN) {
                    struct input_event ev;
                    if (read(physical_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                        if (ev.type == EV_ABS) {
                            if (ev.code == ABS_MT_SLOT) current_slot = (ev.value < 10) ? ev.value : 9;
                            else if (ev.code == ABS_MT_TRACKING_ID) {
                                if (ev.value == -1) {
                                    phys_slot_active[current_slot] = 0;
                                    send_to_client(client_fd, EVENT_TYPE_TOUCH_UP, 0, 0, current_slot);
                                    last_send[current_slot].tv_sec = 0;
                                } else { phys_slot_active[current_slot] = 1; has_coords[current_slot] = 0; }
                            } else if (ev.code == ABS_MT_POSITION_X) last_x[current_slot] = ev.value;
                            else if (ev.code == ABS_MT_POSITION_Y) { last_y[current_slot] = ev.value; has_coords[current_slot] = 1; }
                        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && phys_slot_active[current_slot] && has_coords[current_slot]) {
                            mark_trace("PHYS_TOUCH_SEND");
                            uint8_t type = (last_send[current_slot].tv_sec == 0) ? EVENT_TYPE_TOUCH_DOWN : EVENT_TYPE_TOUCH_MOVE;
                            send_to_client(client_fd, type, last_x[current_slot], last_y[current_slot], current_slot);
                            gettimeofday(&last_send[current_slot], NULL);
                        }
                    }
                }
            }
        }
    }
}

void cleanup(int s) {
    for(int i = 0; i < touch_count; i++) ioctl(touch_fds[i], EVIOCGRAB, 0);
    for(int i = 0; i < key_count; i++) ioctl(key_fds[i], EVIOCGRAB, 0);
    if (uinput_fd >= 0) { ioctl(uinput_fd, UI_DEV_DESTROY); close(uinput_fd); }
    exit(0);
}

int main() {
    setup_rt_priority();
    init_ashmem_bridge();
    signal(SIGTERM, cleanup); signal(SIGINT, cleanup); signal(SIGPIPE, SIG_IGN);
    trace_fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) return 1;
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_BACK);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_HOMEPAGE);
    for (int i = 1; i < 255; i++) { ioctl(uinput_fd, UI_SET_KEYBIT, i); }
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    struct uinput_user_dev uidev = {0};
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", DEV_NAME);
    uidev.absmax[ABS_MT_POSITION_X] = 719;
    uidev.absmax[ABS_MT_POSITION_Y] = 1279;
    uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;
    uidev.absmax[ABS_MT_PRESSURE] = 255;
    uidev.absmax[ABS_MT_TOUCH_MAJOR] = 255;
    write(uinput_fd, &uidev, sizeof(uidev));
    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) return 1;
    DIR *dir = opendir("/dev/input");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                char p[64]; snprintf(p, sizeof(p), "/dev/input/%s", ent->d_name);
                int fd = open(p, O_RDWR | O_NONBLOCK);
                if (fd >= 0) {
                    char n[256]; ioctl(fd, EVIOCGNAME(sizeof(n)), n);
                    if (strcmp(n, DEV_NAME) == 0) { close(fd); continue; }
                    unsigned long abs_bit[NBITS(ABS_MAX)] = {0};
                    unsigned long key_bit[NBITS(KEY_MAX)] = {0};
                    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bit)), abs_bit);
                    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bit)), key_bit);
                    bool is_touch = test_bit(ABS_MT_POSITION_X, abs_bit);
                    bool is_key = test_bit(KEY_POWER, key_bit) || test_bit(KEY_VOLUMEDOWN, key_bit);
                    if (is_touch && touch_count < 8) {
                        touch_fds[touch_count++] = fd;
                        physical_fds[physical_count++] = fd;
                    } else if (is_key && key_count < 8) {
                        key_fds[key_count++] = fd;
                        physical_fds[physical_count++] = fd;
                    } else {
                        close(fd);
                    }
                }
            }
        }
        closedir(dir);
    }
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int v = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(CONTROL_PORT), .sin_addr.s_addr = INADDR_ANY};
    bind(srv, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv, 1);
    fprintf(stderr, "[hw_resident] Ashmem + 13-byte Protocol Active\n");
    run_daemon_loop(srv);
    return 0;
}
