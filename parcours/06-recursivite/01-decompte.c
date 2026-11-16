#include <stdio.h>

void compte(int n)
{
    if (n == 0)
        return;
    // ? n
    putchar('0' + n);
    compte(n - 1);
}

int main(void)
{
    compte(3);
    putchar('\n');
    return 0;
}
