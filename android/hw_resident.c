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
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/system_properties.h>
#include <sys/time.h>

#define CONTROL_PORT 22222
#define CONTROL_MAGIC 0x41444253 // "ADBS"
#define PROTOCOL_HEAD 0x55
#define MAX_FDS 64

#define EVENT_TYPE_KEY           1
#define EVENT_TYPE_TOUCH_DOWN    2
#define EVENT_TYPE_TOUCH_UP      3
#define EVENT_TYPE_TOUCH_MOVE    4
#define EVENT_TYPE_SCROLL        5
#define EVENT_TYPE_BACK          6
#define EVENT_TYPE_HOME          7
#define EVENT_TYPE_ADB_WIFI      20
#define EVENT_TYPE_SET_GRAB      21
#define EVENT_TYPE_REPORT_TOUCH  30

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

int physical_fds[64];
int physical_count = 0;
int uinput_fd = -1;
int active_slots[10] = {0};

void sys_log(const char* m) { fprintf(stderr, "[hw_resident] %s\n", m); }

uint8_t calculate_crc(const struct ControlPacket* pkt) {
    size_t data_len = offsetof(struct ControlPacket, crc) - offsetof(struct ControlPacket, magic);
    const uint8_t *data_ptr = (const uint8_t*)&pkt->magic;
    uint8_t crc = 0;
    for (size_t i = 0; i < data_len; i++) {crc ^= data_ptr[i];}
    return crc;}

void emit(int type, int code, int val) {
    struct input_event ie = {0};
    gettimeofday(&ie.time, NULL);
    ie.type = type; ie.code = code; ie.value = val;
    write(uinput_fd, &ie, sizeof(ie));}

int init_uinput() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, KEY_BACK);
	ioctl(fd, UI_SET_KEYBIT, KEY_HOMEPAGE);
	for(int i=0; i<255; i++) ioctl(fd, UI_SET_KEYBIT, i);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
    struct uinput_user_dev uidev = {0};
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, " 🕷️ 💉hw_keyboard");
    uidev.absmax[ABS_MT_POSITION_X] = 4096;
    uidev.absmax[ABS_MT_POSITION_Y] = 4096;
    uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmax[ABS_MT_TRACKING_ID] = 65535;
    uidev.absmax[ABS_MT_PRESSURE] = 255;
    uidev.absmax[ABS_MT_TOUCH_MAJOR] = 255;
    write(fd, &uidev, sizeof(uidev));
    ioctl(fd, UI_DEV_CREATE);
    return fd;}

void handle_touch(struct ControlPacket* pkt) {
    uint16_t slot = ntohs(pkt->data);
    uint16_t x = ntohs(pkt->x);
    uint16_t y = ntohs(pkt->y);
    emit(EV_ABS, ABS_MT_SLOT, slot);
    if (pkt->type == EVENT_TYPE_TOUCH_DOWN) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, slot + 1);
        emit(EV_KEY, BTN_TOUCH, 1);
        emit(EV_ABS, ABS_MT_PRESSURE, 100);
        emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 5);
        emit(EV_ABS, ABS_MT_POSITION_X, x);
        emit(EV_ABS, ABS_MT_POSITION_Y, y);
    } else if (pkt->type == EVENT_TYPE_TOUCH_MOVE) {
        emit(EV_ABS, ABS_MT_POSITION_X, x);
        emit(EV_ABS, ABS_MT_POSITION_Y, y);
    } else if (pkt->type == EVENT_TYPE_TOUCH_UP) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_KEY, BTN_TOUCH, 0);
    }
    emit(EV_SYN, SYN_REPORT, 0);}

void process_packet(struct ControlPacket* pkt) {
    if (pkt->head != PROTOCOL_HEAD || ntohl(pkt->magic) != CONTROL_MAGIC) return;
    if (calculate_crc(pkt) != pkt->crc) return;
    uint16_t d = ntohs(pkt->data);
    switch (pkt->type) {
		case EVENT_TYPE_TOUCH_DOWN:
		case EVENT_TYPE_TOUCH_MOVE:
		case EVENT_TYPE_TOUCH_UP:
			handle_touch(pkt); break;
		case EVENT_TYPE_KEY: {
			uint16_t is_pressed = ntohs(pkt->x);
			emit(EV_KEY, d, is_pressed);
			emit(EV_SYN, SYN_REPORT, 0);
			break;}
		case EVENT_TYPE_BACK:
            emit(EV_KEY, KEY_BACK, 1); emit(EV_SYN, SYN_REPORT, 0);
			emit(EV_KEY, KEY_BACK, 0); emit(EV_SYN, SYN_REPORT, 0); break;
		case EVENT_TYPE_HOME:
            emit(EV_KEY, KEY_HOMEPAGE, 1); emit(EV_SYN, SYN_REPORT, 0);
            emit(EV_KEY, KEY_HOMEPAGE, 0); emit(EV_SYN, SYN_REPORT, 0); break;
		case EVENT_TYPE_ADB_WIFI:
            __system_property_set("service.adb.tcp.port", "1337");
            __system_property_set("ctl.restart", "adbd");
            sys_log("adb wireless 1337 triggered."); break;
		case EVENT_TYPE_SET_GRAB:
            for(int i=0; i<physical_count; i++) ioctl(physical_fds[i], EVIOCGRAB, d);
            sys_log(d ? "grab on" : "grab off"); break;}}

void run_daemon_loop(int srv_fd) {
    struct pollfd fds[MAX_FDS + 2];
    int client_fd = -1;
    while (1) {
        int poll_count = 0;
        int srv_idx = -1;
        int cli_idx = -1;
        int phys_start_idx = -1;
// Serwer (nasłuchiwanie nowych połączeń)
        fds[poll_count].fd = srv_fd;
        fds[poll_count].events = POLLIN;
        srv_idx = poll_count++;
// Klient (odbieranie komend z Qt)
        if (client_fd >= 0) {
            fds[poll_count].fd = client_fd;
            fds[poll_count].events = POLLIN;
            cli_idx = poll_count++;
        }
// Urządzenia fizyczne (czytanie dotyku z telefonu)
        if (client_fd >= 0) {
            phys_start_idx = poll_count;
            for (int i = 0; i < physical_count; i++) {
                fds[poll_count].fd = physical_fds[i];
                fds[poll_count].events = POLLIN;
                poll_count++;
            }
        }
        if (poll(fds, poll_count, -1) <= 0) continue;
// --- OBSŁUGA NOWEGO POŁĄCZENIA ---
        if (fds[srv_idx].revents & POLLIN) {
            int new_cli = accept(srv_fd, NULL, NULL);
            if (new_cli >= 0) {
                if (client_fd >= 0) close(client_fd);
                client_fd = new_cli;
                sys_log("New controller connected.");
            }
        }
// --- ODBIERANIE DANYCH OD KLIENTA (QT) ---
        if (cli_idx != -1 && (fds[cli_idx].revents & POLLIN)) {
            struct ControlPacket pkt;
            ssize_t n = read(client_fd, &pkt, sizeof(pkt));
            if (n == sizeof(pkt)) {
                process_packet(&pkt);
            } else if (n <= 0) {
                close(client_fd);
                client_fd = -1;
                sys_log("Client disconnected.");
            }
        }
// --- CZYTANIE DOTYKU Z EKRANU TELEFONU ---
        if (client_fd >= 0 && phys_start_idx != -1) {
            for (int i = 0; i < physical_count; i++) {
                if (fds[phys_start_idx + i].revents & POLLIN) {
                    struct input_event ev;
                    if (read(physical_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                        // A. Przesyłanie ruchu (X, Y)
                        if (ev.type == EV_ABS && (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y)) {
                            struct ControlPacket report = {0};
                            report.head = PROTOCOL_HEAD;
                            report.magic = htonl(CONTROL_MAGIC);
                            report.type = EVENT_TYPE_REPORT_TOUCH;
                            report.data = htons(ev.code);
                            if (ev.code == ABS_MT_POSITION_X) report.x = htons(ev.value);
                            else report.y = htons(ev.value);
                            report.crc = calculate_crc(&report);
                            if (write(client_fd, &report, sizeof(report)) < 0) {
                                close(client_fd); client_fd = -1; break;
                            }
                        }
                        // ev.type == 1 (EV_KEY), ev.code == 330 (BTN_TOUCH), ev.value == 0 (RELEASE)
                        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && ev.value == 0) {
                            struct ControlPacket up_pkt = {0};
                            up_pkt.head = PROTOCOL_HEAD;
                            up_pkt.magic = htonl(CONTROL_MAGIC);
                            up_pkt.type = EVENT_TYPE_TOUCH_UP; // Stała równa 3
                            up_pkt.crc = calculate_crc(&up_pkt);
                            
                            sys_log("Touch UP detected, sending to Qt.");
                            if (write(client_fd, &up_pkt, sizeof(up_pkt)) < 0) {
                                close(client_fd); client_fd = -1; break;
                            }
                        }
                    }
                }
            }
        }
    }
}

int main() {
    uinput_fd = init_uinput();
    if (uinput_fd < 0) return 1;
    DIR *dir = opendir("/dev/input");
    struct dirent *ent;
    while (dir && (ent = readdir(dir))) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            char p[64]; snprintf(p, 64, "/dev/input/%s", ent->d_name);
            int fd = open(p, O_RDWR | O_NONBLOCK);
            if (fd >= 0) physical_fds[physical_count++] = fd;
        }
    }
    if(dir) closedir(dir);
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { 
        .sin_family = AF_INET, 
        .sin_port = htons(CONTROL_PORT), 
        .sin_addr.s_addr = INADDR_ANY 
    };
    int v = 1; 
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return 1;
    }
    listen(srv, 1);
    sys_log("🎓--daemon (13-byte protocol) ready.");
    run_daemon_loop(srv); 
    return 0;
}
