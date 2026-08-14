#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    int value = 100;

    printf("Before fork:\n");
    printf("PID: %d\n", getpid());
    printf("value: %d\n\n", value);

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        return 1;
    }

    if (pid == 0)
    {
        value = 200;

        printf("CHILD:\n");
        printf("PID: %d\n", getpid());
        printf("value: %d\n\n", value);

        sleep(300);
    }
    else
    {
        sleep(2);

        printf("PARENT:\n");
        printf("PID: %d\n", getpid());
        printf("value: %d\n\n", value);

        sleep(300);
    }

    return 0;
}