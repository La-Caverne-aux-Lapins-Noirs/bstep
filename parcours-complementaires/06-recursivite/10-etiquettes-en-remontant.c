#include <stdio.h>

void etiquette(int n)
{
    if (n <= 1)
    {
        putchar('A');
        return;
    }
    if (n % 2 == 0)
        putchar('P');
    etiquette(n - 1);
    putchar('0' + n);
}

int main(void)
{
    etiquette(4);
    putchar('\n');
    return 0;
}
