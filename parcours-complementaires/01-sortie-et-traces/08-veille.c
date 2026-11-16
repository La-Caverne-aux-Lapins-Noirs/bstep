#include <stdio.h>

int main(void)
{
    int minutes = 0;

    puts("debut");
    while (minutes > 0)
    {
        putchar('*');
        minutes--;
    }
    printf("fin %d\n", minutes);
    return 0;
}
