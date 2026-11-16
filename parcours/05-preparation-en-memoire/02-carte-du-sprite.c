#include <stdio.h>

int main(void)
{
    char carte[] = "............";
    int x = 1;
    int y = 1;
    int centre = y * 4 + x;

    carte[centre] = 'O';
    carte[centre - 1] = '<';
    carte[centre + 1] = '>';
    // ? centre
    for (int ligne = 0; ligne < 3; ligne++)
    {
        for (int colonne = 0; colonne < 4; colonne++)
            putchar(carte[ligne * 4 + colonne]);
        putchar('\n');
    }
    return 0;
}
