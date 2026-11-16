#include <stdio.h>

int main(void)
{
    int contact;
    int n = 0;

    scanf("%d", &contact);
    while (n < 3)
    {
        if (contact == 1)
        {
            contact = 0;
            putchar('H');
        }
        else
        {
            contact = 1;
            putchar('B');
        }
        n++;
    }
    // ? n, contact
    printf(":%d\n", contact);
    return 0;
}
