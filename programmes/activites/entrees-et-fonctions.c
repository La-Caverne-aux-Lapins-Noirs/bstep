#include <stdio.h>

void repondre(int nombre)
{
    char *mots[] = {"pair", "impair"};
    int parite = nombre % 2 != 0;

    {
        char symbole = mots[parite][0];
        printf("\033[1;36m%c\033[0m ", symbole);
    }

    puts(mots[parite]);
}

int main(void)
{
    int n = 0;
    printf("Nombre entier > ");

    if (scanf("%d", &n) == 1)
        repondre(n);

    return 0;
}
