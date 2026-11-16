#include <stdio.h>

int main(void)
{
    for (int marche = 1; marche <= 3; marche++)
    {
        int poids = 0;

        for (int bloc = 0; bloc < marche; bloc++)
        {
            putchar('#');
            poids += 2;
        }
        // ? marche, poids
        printf(" %d\n", poids);
    }
    return 0;
}
