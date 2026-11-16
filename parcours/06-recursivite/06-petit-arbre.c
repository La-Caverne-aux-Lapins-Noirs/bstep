#include <stdio.h>

void dessine(int niveau)
{
    if (niveau == 0)
    {
        putchar('.');
        return;
    }
    putchar('0' + niveau);
    dessine(niveau - 1);
    dessine(niveau - 1);
    putchar(':');
}

int main(void)
{
    dessine(2);
    putchar('\n');
    return 0;
}
