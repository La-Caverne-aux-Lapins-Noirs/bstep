#include <stdio.h>

int main(void)
{
    char ligne[] = "..#..";
    char suivante[6];
    suivante[5] = '\0';

    for (int tour = 0; tour < 3; tour++)
    {
        puts(ligne);
        for (int x = 0; x < 5; x++)
        {
            int gauche = x > 0 && ligne[x - 1] == '#';
            int droite = x < 4 && ligne[x + 1] == '#';
            suivante[x] = ".#"[gauche != droite];
        }
        for (int x = 0; x < 5; x++)
            ligne[x] = suivante[x];
    }
    return 0;
}
