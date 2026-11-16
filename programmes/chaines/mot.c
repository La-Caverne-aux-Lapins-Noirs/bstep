#include <stdio.h>

int main(void)
{
    char mot[] = "SALUT";
    char *p = mot;
    int i = 0;

    while (mot[i] != '\0')
    {
        *p = *p + ('a' - 'A');
        p++;
        i++;
    }

    puts(mot);
    return 0;
}
