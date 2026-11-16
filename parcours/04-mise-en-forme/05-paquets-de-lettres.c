#include <stdio.h>

int main(void)
{
    char texte[] = "AAABBAC";
    char courant = texte[0];
    int repetition = 1;
    int i = 1;

    while (texte[i] != '\0')
    {
        if (texte[i] == courant)
            repetition++;
        else
        {
            printf("%c%d ", courant, repetition);
            courant = texte[i];
            repetition = 1;
        }
        i++;
    }
    // ? i, repetition
    printf("%c%d\n", courant, repetition);
    return 0;
}
