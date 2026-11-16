#include <stdio.h>

int main(void)
{
    int val = 1;

    for (int tour = 0; tour < 3; tour++)
    {
        printf("t%d ", val);
        if (val == 3)
            putchar('!');
        val = val + 2;
    }
    printf("\nfin %d\n", val);
    return 0;
}
