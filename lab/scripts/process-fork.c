#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>   /* for pid_t */

int main(void)
{
    printf("Before fork()\n");
    printf("PID: %d\n", getpid());

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork failed");
        return 1;
    }

    if (pid == 0)
    {
        printf("I am the CHILD process.\n");
        printf("Child PID: %d\n", getpid());
        printf("Child PPID: %d\n", getppid());
    }
    else
    {
        printf("I am the PARENT process.\n");
        printf("Parent PID: %d\n", getpid());
        printf("Child PID: %d\n", pid);
    }

    sleep(300);

    return 0;
}