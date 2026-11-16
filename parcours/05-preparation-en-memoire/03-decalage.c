#include <stdio.h>

int main(void)
{
    char texte[] = "AB...";
    int i = 2;

    while (i > 0)
    {
        texte[i] = texte[i - 1];
        i--;
    }
    texte[0] = '.';
    // ? i
    puts(texte);
    return 0;
}
