#include <stdio.h>

int main(void)
{
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            int dx = x;
            int dy = y;
            if (dx < 0)
                dx = -dx;
            if (dy < 0)
                dy = -dy;
            if (dx + dy <= 2)
                putchar('#');
            else
                putchar('.');
        }
        putchar('\n');
    }
    return 0;
}
