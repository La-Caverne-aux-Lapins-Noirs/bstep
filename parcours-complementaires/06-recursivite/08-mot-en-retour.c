#include <stdio.h>

void retourne(char *mot, int i)
{
    if (mot[i] == '\0')
        return;
    retourne(mot, i + 1);
    putchar(mot[i]);
}

int main(void)
{
    char mot[] = "LUNE";
    retourne(mot, 0);
    putchar('\n');
    return 0;
}
