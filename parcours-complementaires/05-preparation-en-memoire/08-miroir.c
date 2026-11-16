#include <stdio.h>

int main(void)
{
    char motif[] = "ab#";
    char bande[7];

    for (int i = 0; i < 3; i++)
    {
        bande[i] = motif[i];
        bande[5 - i] = motif[i];
    }
    bande[6] = '\0';
    puts(bande);
    return 0;
}
