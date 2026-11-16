#include <stdio.h>

int main(void)
{
    char titre[] = "RADAR";
    int n = 0;

    while (titre[n] != '\0')
    {
        putchar(titre[n]);
        n++;
    }
    putchar('\n');
    // ? n
    for (int i = 0; i < n; i++)
        putchar('=');
    putchar('\n');
    return 0;
}
