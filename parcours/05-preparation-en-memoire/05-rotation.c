#include <stdio.h>

int main(void)
{
    char avant[] = "ABCDEF";
    char apres[6];

    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 3; x++)
        {
            int index = x * 2 + 1 - y;
            apres[index] = avant[y * 3 + x];
        }
    }
    for (int i = 0; i < 6; i++)
    {
        putchar(apres[i]);
        if (i % 2 != 0)
            putchar('\n');
    }
    return 0;
}
