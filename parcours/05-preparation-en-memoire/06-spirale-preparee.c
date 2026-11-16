#include <stdio.h>

int main(void)
{
    char grille[49];
    int segments[] = {6, 6, 6, 4, 4, 2, 2};
    int x = 0;
    int y = 0;
    int dx = 1;
    int dy = 0;

    for (int i = 0; i < 49; i++)
        grille[i] = '.';
    grille[0] = '#';
    for (int s = 0; s < 7; s++)
    {
        for (int p = 0; p < segments[s]; p++)
        {
            x += dx;
            y += dy;
            grille[y * 7 + x] = '#';
        }
        int ancien_dx = dx;
        dx = -dy;
        dy = ancien_dx;
    }
    // ? x, y
    for (int i = 0; i < 49; i++)
    {
        putchar(grille[i]);
        if (i % 7 == 6)
            putchar('\n');
    }
    return 0;
}
