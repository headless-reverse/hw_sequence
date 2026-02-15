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
#include <netinet/in.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <stdbool.h>

#define CONTROL_PORT 22222
#define CONTROL_MAGIC 0x41444253
#define PROTOCOL_HEAD 0x55
#define MAX_FDS 64
#define DEV_NAME "hw_resident"

#define EVENT_TYPE_KEY           1
#define EVENT_TYPE_TOUCH_DOWN    2
#define EVENT_TYPE_TOUCH_UP      3
#define EVENT_TYPE_TOUCH_MOVE    4
#define EVENT_TYPE_BACK          6
#define EVENT_TYPE_HOME          7
#define EVENT_TYPE_ADB_WIFI      20
#define EVENT_TYPE_SET_GRAB      21
#define EVENT_TYPE_SHELL         40
#define EVENT_TYPE_REC_START     41
#define EVENT_TYPE_REC_STOP      42
#define EVENT_TYPE_REPLAY        43
#define EVENT_TYPE_SET_MODE      50
#define EVENT_TYPE_FORCE_DESYNC  51

#define DEV_TYPE_TOUCH           1
#define DEV_TYPE_KEYS            2

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

void emit(int type, int code, int val);

typedef enum {
    MODE_NORMAL = 0,
    MODE_RAW_MUTATION = 1,
    MODE_FAULT_INJECTION = 2,
    MODE_STATE_DESYNC = 3
} daemon_mode_t;

static daemon_mode_t g_mode = MODE_NORMAL;

typedef struct {
    int fd;
    int type;
} physical_dev_t;

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

struct fast_shell_job { char *cmd; };
struct fast_job { char *payload; };
static int slot_active[10] = {0};
static int active_slots = 0;

int physical_fds[MAX_FDS];
int physical_count = 0;
int uinput_fd = -1;
static int rec_fd = -1;
static int trace_fd = -1;
static int global_tracking_id_counter = 100;

void sys_log(const char* m) { fprintf(stderr, "[hw_resident] %s\n", m); }

static inline ssize_t raw_write(int fd, const void *buf, size_t count) {
    return syscall(__NR_write, fd, buf, count);
}

void setup_rt_priority() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
    struct sched_param param;
    param.sched_priority = 90;
    sched_setscheduler(0, SCHED_FIFO, &param);
    mlockall(MCL_CURRENT | MCL_FUTURE);
    sys_log("RT priority + CPU affinity (CPU3) set.");
}

void init_tracing() {
    trace_fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
}

void force_inconsistent_state() {
    emit(EV_ABS, ABS_MT_SLOT, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 500);
    emit(EV_ABS, ABS_MT_SLOT, 1);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 500);
    emit(EV_SYN, SYN_REPORT, 0);
    sys_log("Injected inconsistent slot state");
}

static void *fast_shell_thread(void *arg) {
    struct fast_shell_job *j = (struct fast_shell_job*)arg;
    if (j->cmd) system(j->cmd);
    free(j->cmd); free(j);
    return NULL;
}

static void *replay_thread(void *arg) {
    struct fast_job *j = (struct fast_job*)arg;
    int fd = open(j->payload, O_RDONLY);
    if (fd < 0) { free(j->payload); free(j); return NULL; }
    uint64_t last_ts = 0;
    while (1) {
        uint64_t sec, usec; struct input_event ev;
        if (read(fd, &sec, sizeof(sec)) <= 0) break;
        if (read(fd, &usec, sizeof(usec)) != sizeof(usec)) break;
        if (read(fd, &ev, sizeof(ev)) <= 0) break;
        uint64_t current_ts = (sec * 1000000) + usec;
        if (last_ts != 0 && current_ts > last_ts) {
            uint64_t delta = current_ts - last_ts;
            if (delta > 2000000) delta = 2000000;
            struct timespec ts = { .tv_sec = delta / 1000000, .tv_nsec = (delta % 1000000) * 1000 };
            nanosleep(&ts, NULL);
        }
        last_ts = current_ts;
        gettimeofday(&ev.time, NULL);
        raw_write(uinput_fd, &ev, sizeof(ev));
    }
    close(fd); free(j->payload); free(j); return NULL;
}

static void schedule_fast_shell(const char *cmd) {
    struct fast_shell_job *j = malloc(sizeof(*j));
    if (!j) return;
    j->cmd = strdup(cmd);
    pthread_t t; pthread_create(&t, NULL, fast_shell_thread, j);
    pthread_detach(t);}

static void schedule_replay(const char *path) {
	struct fast_job *j = malloc(sizeof(*j));
	if (!j) return;
	j->payload = strdup(path);
	if (!j->payload) {
		free(j);
		return;}
    pthread_t t; pthread_create(&t, NULL, replay_thread, j);
    pthread_detach(t);}

void record_event(const struct input_event *ev) {
    if (rec_fd < 0) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t sec = (uint64_t)tv.tv_sec;
    uint64_t usec = (uint64_t)tv.tv_usec;
    write(rec_fd, &sec, sizeof(sec));
    write(rec_fd, &usec, sizeof(usec));
    write(rec_fd, ev, sizeof(*ev));}

int start_record(const char *path) {
    rec_fd = open(path, O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (rec_fd >= 0) sys_log("Recording started.");
    return rec_fd >= 0;}

void stop_record() {
    if (rec_fd >= 0) { close(rec_fd); rec_fd = -1; sys_log("Recording stopped."); }}

uint8_t calculate_crc(const struct ControlPacket* pkt) {
    size_t data_len = offsetof(struct ControlPacket, crc) - offsetof(struct ControlPacket, magic);
    const uint8_t *data_ptr = (const uint8_t*)&pkt->magic;
    uint8_t crc = 0;
    for (size_t i = 0; i < data_len; i++) { crc ^= data_ptr[i]; }
    return crc;}

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

void emit(int type, int code, int val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = val;

    ssize_t r;
    do {
        r = raw_write(uinput_fd, &ie, sizeof(ie));
    } while (r < 0 && (errno == EAGAIN || errno == EINTR));
}

void mark_trace(const char *msg) {
    int fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

void handle_mt_logic(struct ControlPacket* pkt) {
    uint16_t slot = ntohs(pkt->data) % 10;
    uint16_t x = ntohs(pkt->x);
    uint16_t y = ntohs(pkt->y);
    emit(EV_ABS, ABS_MT_SLOT, slot);
    if (pkt->type == EVENT_TYPE_TOUCH_DOWN) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, global_tracking_id_counter++);
        if (global_tracking_id_counter > 65000)
            global_tracking_id_counter = 100;
        emit(EV_ABS, ABS_MT_POSITION_X, x);
        emit(EV_ABS, ABS_MT_POSITION_Y, y);
        emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 5);
        emit(EV_ABS, ABS_MT_PRESSURE, 50);
        if (!slot_active[slot]) {
            slot_active[slot] = 1;
            active_slots++;
        }
        if (active_slots == 1)
            emit(EV_KEY, BTN_TOUCH, 1);
    } 
    else if (pkt->type == EVENT_TYPE_TOUCH_MOVE) {
        if (slot_active[slot]) {
            emit(EV_ABS, ABS_MT_POSITION_X, x);
            emit(EV_ABS, ABS_MT_POSITION_Y, y);
            emit(EV_ABS, ABS_MT_PRESSURE, 50);
        }
    } 
    else if (pkt->type == EVENT_TYPE_TOUCH_UP) {

        if (slot_active[slot]) {
            emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            slot_active[slot] = 0;
            active_slots--;
            if (active_slots < 0) active_slots = 0;
        }
        if (active_slots == 0)
            emit(EV_KEY, BTN_TOUCH, 0);
    }
    emit(EV_SYN, SYN_MT_REPORT, 0);
    emit(EV_SYN, SYN_REPORT, 0);
}

int init_uinput() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, KEY_BACK);
    ioctl(fd, UI_SET_KEYBIT, KEY_HOMEPAGE);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    struct uinput_user_dev uidev = {0};
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", DEV_NAME);
    uidev.absmax[ABS_MT_POSITION_X] = 4096;
    uidev.absmax[ABS_MT_POSITION_Y] = 4096;
    uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;
    uidev.absmax[ABS_MT_PRESSURE] = 255;
    uidev.absmax[ABS_MT_TOUCH_MAJOR] = 255;
    write(fd, &uidev, sizeof(uidev));
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

void process_packet(struct ControlPacket* pkt) {
    if (pkt->head != PROTOCOL_HEAD) return;
    uint16_t d = ntohs(pkt->data);
    switch (pkt->type) {
        case EVENT_TYPE_TOUCH_DOWN:
        case EVENT_TYPE_TOUCH_MOVE:
        case EVENT_TYPE_TOUCH_UP: handle_mt_logic(pkt); break;
        case EVENT_TYPE_KEY:emit(EV_KEY, d, ntohs(pkt->x));emit(EV_SYN, SYN_REPORT, 0);break;
        case EVENT_TYPE_BACK:     emit(EV_KEY, KEY_BACK, 1); emit(EV_SYN, SYN_REPORT, 0); emit(EV_KEY, KEY_BACK, 0); emit(EV_SYN, SYN_REPORT, 0); break;
        case EVENT_TYPE_HOME:     emit(EV_KEY, KEY_HOMEPAGE, 1); emit(EV_SYN, SYN_REPORT, 0); emit(EV_KEY, KEY_HOMEPAGE, 0); emit(EV_SYN, SYN_REPORT, 0); break;
        case EVENT_TYPE_ADB_WIFI: __system_property_set("service.adb.tcp.port", "1337"); __system_property_set("ctl.restart", "adbd"); break;
        case EVENT_TYPE_SET_MODE: g_mode = (daemon_mode_t)d; break;
        case EVENT_TYPE_FORCE_DESYNC: force_inconsistent_state(); break;
        case EVENT_TYPE_REC_START: rec_fd = open("/data/local/tmp/macro.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644); break;
        case EVENT_TYPE_REC_STOP:  if(rec_fd >= 0) { close(rec_fd); rec_fd = -1; } break;
        case EVENT_TYPE_REPLAY: {
            struct fast_job *j = malloc(sizeof(*j)); j->payload = strdup("/data/local/tmp/macro.bin");
            pthread_t t; pthread_create(&t, NULL, replay_thread, j); pthread_detach(t);
        } break;
        case EVENT_TYPE_SET_GRAB:for (int i = 0; i < physical_count; i++)ioctl(physical_fds[i], EVIOCGRAB, (int)d);break;
    }
}

void run_daemon_loop(int srv_fd) {
    struct pollfd fds[MAX_FDS + 2];
    int client_fd = -1;
    static int last_x[10], last_y[10], slot_active[10], has_coords[10];
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
            int n = accept(srv_fd, NULL, NULL);
            if (n >= 0) {
                if (client_fd >= 0) close(client_fd);
                client_fd = n;
                sys_log("Client connected.");
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
			if (total == sizeof(struct ControlPacket)) {
				if (ntohl(pkt.magic) != CONTROL_MAGIC)
				continue;
				if (calculate_crc(&pkt) != pkt.crc)
				continue;
				process_packet(&pkt);
				
            } else if (total == 0 || (total < 13 && total > 0)) {
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
                                    slot_active[current_slot] = 0;
                                    send_to_client(client_fd, EVENT_TYPE_TOUCH_UP, 0, 0, current_slot);
                                    last_send[current_slot].tv_sec = 0;
                                } else { slot_active[current_slot] = 1; has_coords[current_slot] = 0; }
                            } else if (ev.code == ABS_MT_POSITION_X) last_x[current_slot] = ev.value;
                            else if (ev.code == ABS_MT_POSITION_Y) { last_y[current_slot] = ev.value; has_coords[current_slot] = 1; }
                        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && slot_active[current_slot] && has_coords[current_slot]) {
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
    for(int i = 0; i < physical_count; i++) ioctl(physical_fds[i], EVIOCGRAB, 0);
    if (uinput_fd >= 0) { ioctl(uinput_fd, UI_DEV_DESTROY); close(uinput_fd); }
    exit(0);
}

int main() {
    setup_rt_priority();
    signal(SIGTERM, cleanup); 
    signal(SIGINT, cleanup);
    trace_fd = open("/sys/kernel/debug/tracing/trace_marker", O_WRONLY);
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        perror("Błąd otwarcia /dev/uinput");
        return 1;
    }
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_BACK);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_HOMEPAGE);
    for (int i = 1; i < 255; i++) {
        ioctl(uinput_fd, UI_SET_KEYBIT, i);
    }
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
    uidev.absmax[ABS_MT_POSITION_X] = 4096;
    uidev.absmax[ABS_MT_POSITION_Y] = 4096;
    uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;
    uidev.absmax[ABS_MT_PRESSURE] = 255;
    uidev.absmax[ABS_MT_TOUCH_MAJOR] = 255;
    if (write(uinput_fd, &uidev, sizeof(uidev)) < 0) {
        perror("Błąd zapisu uidev");
        return 1;
    }
    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        perror("Błąd UI_DEV_CREATE");
        return 1;
    }
    DIR *dir = opendir("/dev/input");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                char p[64]; 
                snprintf(p, sizeof(p), "/dev/input/%s", ent->d_name);
                int fd = open(p, O_RDWR | O_NONBLOCK);
                if (fd >= 0) {
                    char n[256];
                    ioctl(fd, EVIOCGNAME(sizeof(n)), n);
                    if (strcmp(n, DEV_NAME) == 0) {
                        close(fd);
                        continue;
                    }
                    unsigned long abs[NBITS(ABS_MAX)] = {0};
                    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
                    if (test_bit(ABS_MT_POSITION_X, abs)) {
                        if (physical_count < MAX_FDS) {
                            physical_fds[physical_count++] = fd;
                            fprintf(stderr, "[hw_resident] Found physical touch: %s (%s)\n", p, n);
                        } else {
                            close(fd);
                        }
                    } else {
                        close(fd);
                    }
                }
            }
        }
        closedir(dir);
    }
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int v = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    if (listen(srv, 1) < 0) return 1;
    sys_log("🎓--daemon (13-byte protocol) ready. (optimized 250Hz)");
    run_daemon_loop(srv);
    return 0;
}
