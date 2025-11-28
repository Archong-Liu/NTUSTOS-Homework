#include "syscall.h"

#define N 1000   // 2k integers = 8KB，保證多頁

int bigArray[N];    // 放 BSS，不佔 stack，不會 crash

int main() {
    int i;
    int sum;

    sum = 0;

    for (i = 0; i < N; i++) {
        bigArray[i] = i;
    }

    for (i = 0; i < N; i++) {
        sum += bigArray[i];
    }

    PrintInt(sum);
    Halt();
    return 0;
}
