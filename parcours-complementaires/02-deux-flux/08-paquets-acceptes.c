#include <stdio.h>

int main(void)
{
    int total = 0;
    int poids[] = {2, 7, 1, 4};

    for (int i = 0; i < 4; i++)
    {
        if (poids[i] <= 3)
        {
            putc('o', stdout);
            total += poids[i];
        }
        else
            putc('x', stderr);
    }
    // ? total
    printf(" %d\n", total);
    return 0;
}
