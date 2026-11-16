#include <stdio.h>

int main(void)
{
    int solde = 4;
    int debit;
    int rejets = 0;

    for (int op = 0; op < 3; op++)
    {
        scanf("%d", &debit);
        if (debit <= solde && debit > 0)
        {
            solde -= debit;
            printf("%d ", solde);
        }
        else
        {
            rejets++;
            fprintf(stderr, "r%d ", op);
        }
    }
    // ? solde, rejets
    printf("OK %d\n", 3 - rejets);
    return 0;
}
