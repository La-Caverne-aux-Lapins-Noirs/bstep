#include <stdio.h>

int main(void)
{
    char grille[] = ".........";
    int cases[] = {4, 1, 7, 3};
    char pion = 'X';

    for (int tour = 0; tour < 4; tour++)
    {
        grille[cases[tour]] = pion;
        if (pion == 'X')
            pion = 'O';
        else
            pion = 'X';
    }
    for (int i = 0; i < 9; i++)
    {
        putchar(grille[i]);
        if (i % 3 == 2)
            putchar('\n');
    }
    return 0;
}
