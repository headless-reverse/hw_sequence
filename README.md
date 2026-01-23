# hw_sequence
hybrid engine for standard ADB/Shell commands and low-level IOCTL hardware injection (Kernel level). Includes a binary daemon for raw key/touch event simulation, real-time video streaming, and a sequence builder for precise, undetectable task automation


# hw_sequence
hybrid engine for standard ADB/Shell commands and low-level IOCTL hardware injection (Kernel level). Includes a binary daemon for raw key/touch event simulation, real-time video streaming, and a sequence builder for precise, undetectable task automation

**init.rc**
```
## Daemon processes to be run by init.

# ------> --daemon (13-byte protocol) <---------
service hw_resident /system/bin/hw_resident
    class core
    user root
    group root input system inet net_raw radio
    seclabel u:r:init:s0
    disabled
    restart_period 5

on post-fs-data
    start hw_resident
# ------> --daemon (13-byte protocol) <---------
```

**websockret w budowie**
```
# /etc/systemd/system/adb_sequence.service

[Unit]
Description=ADB Sequence Automation Server
After=network.target

[Service]
Type=simple

User=phantom
Group=phantom

# Argumenty --server i --port 12345 mogą nadpisać plik konfiguracyjny,
ExecStart=/usr/local/bin/adb_sequence_d --server --port 12345

Restart=always
RestartSec=5

StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

```
# /usr/local/etc/adb_sequence.conf
#./adb_sequence_d --server --port 12345

[Settings]
# Ta wartość może zostać nadpisana przez flagę CLI: --adb-path
adbPath=/usr/bin/adb

# Numer seryjny . Jeśli puste, ADB wybierze jedyne dostępne.
# nadpisać flagą CLI: --device-serial
targetSerial=

# nadpisać flagą CLI: --port
serverPort=12345
```
