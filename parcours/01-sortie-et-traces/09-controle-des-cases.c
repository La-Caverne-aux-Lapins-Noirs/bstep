#include <stdio.h>

int main(void)
{
    int valides = 0;

    for (int ligne = 1; ligne <= 2; ligne++)
    {
        for (int colonne = 1; colonne <= 3; colonne++)
        {
            if ((ligne + colonne) % 2 == 0)
            {
                valides++;
                putchar('O');
            }
            else
                putchar('.');
        }
        // ? ligne, valides
        printf(" %d\n", valides);
    }
    return 0;
}
