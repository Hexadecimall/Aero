typedef unsigned long word;
static const char* volatile banner = "Aero\n";

static long writeText(const char* text, word length) {
    register word arg0 __asm__("x0") = 1;
    register const char* arg1 __asm__("x1") = text;
    register word arg2 __asm__("x2") = length;
    register word callNumber __asm__("x8") = 64;
    __asm__ volatile("svc #0" : "+r"(arg0) : "r"(arg1), "r"(arg2), "r"(callNumber) : "memory", "cc");
    return (long)arg0;
}

__attribute__((noinline)) static word factorial(word value) {
    if (value < 2) return 1;
    return value * factorial(value - 1);
}

__attribute__((noinline)) static word checksum(const char* text) {
    word result = 5381;
    while (*text) result = (result * 33) ^ (unsigned char)*text++;
    return result;
}

static void printNumber(word value) {
    char buffer[32];
    word length = 0;
    do {
        buffer[length++] = '0' + value % 10;
        value /= 10;
    } while (value);
    for (word index = 0; index < length / 2; ++index) {
        char saved = buffer[index];
        buffer[index] = buffer[length - index - 1];
        buffer[length - index - 1] = saved;
    }
    writeText(buffer, length);
    writeText("\n", 1);
}

long programMain(word count, char** args) {
    writeText(banner, 5);
    volatile word values[4] = {3, 7, 11, 13};
    word total = 0;
    for (word index = 0; index < 4; ++index) total += values[index];
    printNumber(total);
    printNumber(factorial(count + 4));
    for (word index = 1; index < count; ++index) printNumber(checksum(args[index]));
    return 0;
}
