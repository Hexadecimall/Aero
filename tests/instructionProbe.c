typedef unsigned long word;
extern void instructionProbe(word* output);

#ifdef aeroHostOracle
#include <stdio.h>
int main(void) {
    word output[24];
    instructionProbe(output);
    return fwrite(output, sizeof(output), 1, stdout) == 1 ? 0 : 1;
}
#else
long programMain(word count, char** args) {
    (void)count;
    (void)args;
    word output[24];
    instructionProbe(output);
    register word arg0 __asm__("x0") = 1;
    register word* arg1 __asm__("x1") = output;
    register word arg2 __asm__("x2") = sizeof(output);
    register word callNumber __asm__("x8") = 64;
    __asm__ volatile("svc #0" : "+r"(arg0) : "r"(arg1), "r"(arg2), "r"(callNumber) : "memory", "cc");
    return arg0 == sizeof(output) ? 0 : 1;
}
#endif
