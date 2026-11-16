#include <stdio.h>

int main(void)
{
    for (int n = 1; n <= 5; n++)
    {
        if (n > 1)
            putchar('-');
        printf("%d", n);
    }
    putchar('\n');
    return 0;
}
