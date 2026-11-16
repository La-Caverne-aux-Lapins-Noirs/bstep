#include <stdio.h>

int main(void)
{
    char ligne[] = ".....";
    int position = 1;

    ligne[position] = 'A';
    position += 2;
    ligne[position] = 'B';
    ligne[position - 2] = '#';
    // ? position
    puts(ligne);
    return 0;
}
