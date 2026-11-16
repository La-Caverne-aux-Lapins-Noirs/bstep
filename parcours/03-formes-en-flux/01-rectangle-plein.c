#include <stdio.h>

int main(void)
{
    int hauteur = 2;
    int largeur = 4;

    for (int y = 0; y < hauteur; y++)
    {
        for (int x = 0; x < largeur; x++)
            putchar('#');
        putchar('\n');
    }
    return 0;
}
