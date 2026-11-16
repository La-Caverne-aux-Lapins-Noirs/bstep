#include <stdio.h>

int main(void)
{
    for (int z = 0; z <= 2; z += 2)
    {
        int rayon2 = 4 - z * z;
        // ? z, rayon2
        printf("z=%d\n", z);
        for (int y = -2; y <= 2; y++)
        {
            for (int x = -2; x <= 2; x++)
            {
                if (x * x + y * y <= rayon2)
                    putchar('#');
                else
                    putchar('.');
            }
            putchar('\n');
        }
    }
    return 0;
}
