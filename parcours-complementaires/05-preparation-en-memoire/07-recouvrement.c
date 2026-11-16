#include <stdio.h>

int main(void)
{
    char lettre[] = "...";

    for (int i = 0; i < 3; i++)
        lettre[i] = 'x';
    lettre[1] = 'o';
    lettre[2] = lettre[1];
    puts(lettre);
    return 0;
}
