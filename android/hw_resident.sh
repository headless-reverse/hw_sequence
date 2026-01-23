#!/bin/bash

SDK_PATH="/home/phantom/Android/Sdk"

CLANG_64=$(find $SDK_PATH/ndk -name "aarch64-linux-android30-clang" | head -n 1)
CLANG_32=$(find $SDK_PATH/ndk -name "armv7a-linux-androideabi30-clang" | head -n 1)

if [ -z "$CLANG_64" ] || [ -z "$CLANG_32" ]; then
    echo "BŁĄD: Nie znaleziono kompilatorów NDK (clang) w $SDK_PATH/ndk"
    exit 1
fi

echo "--- START KOMPILACJI  ---"

echo "Kompiluję hw_resident_64..."
$CLANG_64 -O3 -static hw_resident.c -o hw_resident_64
if [ $? -eq 0 ]; then echo "OK: hw_resident_64 gotowy."; else echo "BŁĄD kompilacji 64-bit"; fi

echo "Kompiluję hw_resident_32..."
$CLANG_32 -O3 -static hw_resident.c -o hw_resident_32
if [ $? -eq 0 ]; then echo "OK: hw_resident_32 gotowy."; else echo "BŁĄD kompilacji 32-bit"; fi

echo "--- GOTOWE ---"
echo ""
echo "adb push hw_resident_<32|64> /data/local/tmp/hw_resident"
