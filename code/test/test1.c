#include "syscall.h"
int main() {
    // 故意宣告一個大陣列，超過 4KB（NachOS 預設主記憶體）
    // int 大小 4 bytes -> 200 個 int 約 800 bytes
    const int N = 2000;
    volatile int a[N];
    int i;
    int sum = 0;
    for (i = 0; i < N; i++) {
        a[i] = i;
    }
    for (i = 0; i < N; i++) {
        sum += a[i];
    }
    PrintInt(sum);    // 或使用 printf/Write depending on your NachOS test harness
    Halt();
    return 0;
}