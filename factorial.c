#include <stdio.h>

// Recursive function to calculate factorial
long long factorial(int n) {
    if (n == 0 || n == 1)
        return 1;                  // Base case
    else
        return n * factorial(n - 1); // Recursive case
}

int main() {
    int num;

    // Take input from user
    printf("Enter a number: ");
    scanf("%d", &num);

    // Validate input
    if (num < 0) {
        printf("Factorial is not defined for negative numbers.\n");
    } else {
        long long result = factorial(num);
        printf("Factorial of %d = %lld\n", num, result);
    }

    return 0;
}
