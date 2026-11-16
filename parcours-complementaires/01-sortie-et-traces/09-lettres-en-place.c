#include <stdio.h>

int main(void)
{
    char lettre = 'C';

    putchar(lettre);
    for (int pas = 0; pas < 2; pas++)
    {
        lettre = lettre - 1;
        putchar(lettre);
    }
    putchar('\n');
    return 0;
}
