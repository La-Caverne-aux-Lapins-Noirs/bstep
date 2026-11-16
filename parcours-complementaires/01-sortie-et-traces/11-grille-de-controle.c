#include <stdio.h>

int main(void)
{
    int alertes = 0;

    for (int ligne = 0; ligne < 2; ligne++)
    {
        for (int colonne = 0; colonne < 3; colonne++)
        {
            if (ligne + colonne == 2)
            {
                putchar('!');
                alertes++;
            }
            else
                putchar('.');
        }
        putchar('\n');
    }
    printf("Alertes %d\n", alertes);
    return 0;
}
