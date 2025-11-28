#include "syscall.h"

#define N 1000
volatile int a[N];

int main() {
    int i;
    int sum;

    sum = 0;
    i=0;
    // 調 3 個熱區，不連續
    for (i = 0; i < N; i += 3) a[i] = i;
    for (i = 1; i < N; i += 3) a[i] = i;
    for (i = 2; i < N; i += 3) a[i] = i;

    // 交錯讀取 3 區域
    for (i = 0; i < N; i++) {
        sum += a[(i*7) % N]; // 用跳躍式 pattern
    }

    PrintInt(sum);
    Halt();
    return 0;
}
