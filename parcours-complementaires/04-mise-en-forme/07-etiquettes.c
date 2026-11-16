#include <stdio.h>

int main(void)
{
    int portes[] = {3, 10, 7};
    for (int i = 0; i < 3; i++)
    {
        putchar('P');
        if (portes[i] < 10)
            putchar('0');
        printf("%d\n", portes[i]);
    }
    return 0;
}
