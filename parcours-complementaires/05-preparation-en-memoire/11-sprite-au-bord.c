#include <stdio.h>

int main(void)
{
    char cadre[] = "........";
    char sprite[] = "XYZ";
    int places = 0;

    for (int y = 0; y < 2; y++)
    {
        int bord = 2 - 3 * y;
        for (int i = 0; i < 3; i++)
        {
            int x = bord + i;
            if (x >= 0 && x < 4)
            {
                cadre[y * 4 + x] = sprite[i];
                places++;
            }
        }
    }
    // ? places
    for (int i = 0; i < 8; i++)
    {
        putchar(cadre[i]);
        if (i % 4 == 3)
            putchar('\n');
    }
    return 0;
}
