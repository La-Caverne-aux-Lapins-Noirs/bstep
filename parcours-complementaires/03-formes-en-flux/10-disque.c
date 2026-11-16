#include <stdio.h>

int main(void)
{
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            int distance2 = x * x + y * y;
            if (distance2 <= 1)
                putchar('@');
            else if (distance2 <= 4)
                putchar('#');
            else
                putchar('.');
        }
        putchar('\n');
    }
    return 0;
}
