// Freestanding AArch64 Linux init used only by the hardware boot test.
static long guestCall(long number, long a, long b, long c, long d) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x8 __asm__("x8") = number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory", "cc");
    return x0;
}
void _start(void) {
    const char message[] = "AERO_SYSTEM_BOOT_OK\n";
    if (guestCall(172, 0, 0, 0, 0) == 1)
        guestCall(64, 1, (long)message, sizeof(message)-1, 0);
    if (guestCall(40, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0) == 0) {
        long diskFd = guestCall(56, -100, (long)"/dev/vda", 2, 0);
        static char sector[512] = "AERO_DISK_OK";
        long writeResult = diskFd < 0 ? diskFd : guestCall(64, diskFd, (long)sector, sizeof(sector), 0);
        if (writeResult == sizeof(sector) && guestCall(82, diskFd, 0, 0, 0) == 0) {
            guestCall(64, 1, (long)"AERO_DISK_WRITE_OK\n", 18, 0);
        } else if (writeResult < 0 && writeResult != -2) {
            if (diskFd >= 0) guestCall(57, diskFd, 0, 0, 0);
            diskFd = guestCall(56, -100, (long)"/dev/vda", 0, 0);
            sector[0] = sector[11] = 0;
            if (diskFd >= 0 && guestCall(63, diskFd, (long)sector, sizeof(sector), 0) == sizeof(sector) &&
                sector[0] == 'A' && sector[11] == 'K')
                guestCall(64, 1, (long)"AERO_DISK_READ_ONLY_OK\n", 22, 0);
        }
        if (diskFd >= 0) guestCall(57, diskFd, 0, 0, 0);
    }
    guestCall(142, 0xfee1dead, 672274793, 0x4321fedc, 0);
    for (;;) {}
}
