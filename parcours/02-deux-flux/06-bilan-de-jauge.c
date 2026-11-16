#include <stdio.h>

int main(void)
{
    int jauge = 5;
    int tours = 0;

    while (jauge > 0)
    {
        if (jauge % 2 != 0)
            fprintf(stderr, "[%d]", jauge);
        else
            fputc('.', stderr);
        jauge--;
        tours++;
    }
    fprintf(stdout, "Tours %d\n", tours);
    return 0;
}
