#include <stdio.h>

int main(void)
{
    int lettre;
    int ignorees = 0;

    for (int i = 0; i < 4; i++)
    {
        lettre = getchar();
        if (lettre == '?')
        {
            fputc('!', stderr);
            ignorees++;
        }
        else
            putchar(lettre);
    }
    // ? ignorees
    fprintf(stderr, ":%d\n", ignorees);
    return 0;
}
