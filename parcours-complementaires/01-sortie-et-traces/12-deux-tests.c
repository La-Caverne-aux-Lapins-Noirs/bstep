#include <stdio.h>

int main(void)
{
    int compteur = 0;

    for (int seuil = 0; seuil < 5; seuil++)
    {
        if (seuil >= 2 && ++compteur < 3)
            putchar('O');
        else
            putchar('F');
    }
    // ? compteur
    printf(" %d\n", compteur);
    return 0;
}
