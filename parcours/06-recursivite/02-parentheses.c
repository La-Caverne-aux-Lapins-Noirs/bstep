#include <stdio.h>

void entoure(int etages)
{
    if (etages == 0)
    {
        putchar('*');
        return;
    }
    putchar('(');
    entoure(etages - 1);
    putchar(')');
}

int main(void)
{
    entoure(2);
    putchar('\n');
    return 0;
}
