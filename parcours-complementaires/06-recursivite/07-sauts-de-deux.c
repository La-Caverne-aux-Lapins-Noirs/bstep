#include <stdio.h>

void marche(int n)
{
    if (n <= 0)
    {
        putchar('O');
        return;
    }
    putchar('0' + n);
    marche(n - 2);
    putchar('!');
}

int main(void)
{
    marche(5);
    putchar('\n');
    return 0;
}
