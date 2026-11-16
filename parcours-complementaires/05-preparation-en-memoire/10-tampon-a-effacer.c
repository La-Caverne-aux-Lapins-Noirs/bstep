#include <stdio.h>

int main(void)
{
    char bande[] = ".......";
    int depart = 2;
    int limite = 6;
    int effacees = 0;

    for (int x = depart; x < limite; x++)
        bande[x] = '#';
    for (int x = 1; x < 5; x += 2)
    {
        if (bande[x] == '#')
            effacees++;
        bande[x] = '.';
    }
    // ? effacees
    puts(bande);
    return 0;
}
