#include <stdio.h>

void chiffres(int nombre)
{
    if (nombre >= 10)
        chiffres(nombre / 10);
    putchar('0' + nombre % 10);
    putchar(' ');
}

int main(void)
{
    int nombre;
    scanf("%d", &nombre);
    chiffres(nombre);
    putchar('\n');
    return 0;
}
