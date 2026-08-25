/*
 * inode_demo.c
 *
 * Practical assignment: demonstrate that a filename is only a directory entry
 * and that an inode (and its data blocks) stays alive as long as any process
 * still has it open, even after every name has been removed.
 *
 * Compile:  gcc -Wall -O2 -o inode_demo inode_demo.c
 * Run:      ./inode_demo
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

#define WORKDIR_TEMPLATE "/tmp/inode_demo_XXXXXX"

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void print_inode_info(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
        die("stat");
    printf("  %-20s  inode=%lu  links=%lu  size=%ld\n",
           path, (unsigned long)st.st_ino,
           (unsigned long)st.st_nlink, (long)st.st_size);
}

int main(void)
{
    char workdir[] = WORKDIR_TEMPLATE;
    if (mkdtemp(workdir) == NULL)
        die("mkdtemp");

    printf("Working directory: %s\n\n", workdir);

    if (chdir(workdir) == -1)
        die("chdir");

    /* ---------------------------------------------------------------
     * 1. Create a file and record its inode
     * --------------------------------------------------------------- */
    const char *original = "original.txt";
    int fd = open(original, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1)
        die("open original");

    const char *initial = "initial content\n";
    if (write(fd, initial, strlen(initial)) != (ssize_t)strlen(initial))
        die("write initial");
    close(fd);

    struct stat st;
    if (stat(original, &st) == -1)
        die("stat original");

    printf("1. Created file\n");
    print_inode_info(original);
    printf("\n");

    ino_t inode = st.st_ino;

    /* ---------------------------------------------------------------
     * 2. Create three hard links
     * --------------------------------------------------------------- */
    if (link(original, "link1.txt") == -1) die("link1");
    if (link(original, "link2.txt") == -1) die("link2");
    if (link(original, "link3.txt") == -1) die("link3");

    printf("2. After creating three hard links:\n");
    print_inode_info(original);
    print_inode_info("link1.txt");
    print_inode_info("link2.txt");
    print_inode_info("link3.txt");
    printf("\n");

    /* ---------------------------------------------------------------
     * 3. Background process that keeps the file open and writes
     *    a timestamp every few seconds
     * --------------------------------------------------------------- */
    pid_t bg_pid = fork();
    if (bg_pid == -1)
        die("fork");

    if (bg_pid == 0) {
        /* Child: keep fd open and write forever */
        int wfd = open(original, O_WRONLY | O_APPEND);
        if (wfd == -1)
            _exit(1);

        /* Make sure the parent sees the open fd */
        while (1) {
            time_t now = time(NULL);
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "%ld still alive\n", (long)now);
            if (write(wfd, buf, n) != n)
                break;
            sleep(2);
        }
        close(wfd);
        _exit(0);
    }

    printf("3. Background writer started (PID %d)\n", (int)bg_pid);
    sleep(1);   /* give it time to open and write once */

    /* ---------------------------------------------------------------
     * 4. Delete all directory entries
     * --------------------------------------------------------------- */
    unlink(original);
    unlink("link1.txt");
    unlink("link2.txt");
    unlink("link3.txt");

    printf("4. All names removed.\n");
    printf("   Directory contents now:\n");
    DIR *d = opendir(".");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] != '.')
                printf("     %s\n", de->d_name);
        }
        closedir(d);
    }
    printf("   (should be empty of our files)\n\n");

    /* Show that the open file still exists via /proc */
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd", (int)bg_pid);
    printf("   Open file descriptors of the writer (via %s):\n", proc_path);

    DIR *fddir = opendir(proc_path);
    if (fddir) {
        struct dirent *de;
        while ((de = readdir(fddir)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            char linkpath[256], target[256];
            snprintf(linkpath, sizeof(linkpath), "%s/%s", proc_path, de->d_name);
            ssize_t len = readlink(linkpath, target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                printf("     fd %s -> %s\n", de->d_name, target);
            }
        }
        closedir(fddir);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * 5. Show the process continues to write and space is still used
     * --------------------------------------------------------------- */
    printf("5. Waiting a few seconds for more writes...\n");
    sleep(5);

    /* Read the content through the still-open fd in /proc */
    char content_path[128];
    snprintf(content_path, sizeof(content_path),
             "/proc/%d/fd/3", (int)bg_pid);   /* usually fd 3, but we check */

    /* Find the actual open fd that points to our deleted file */
    int found_fd = -1;
    fddir = opendir(proc_path);
    if (fddir) {
        struct dirent *de;
        while ((de = readdir(fddir)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            char linkpath[256], target[256];
            snprintf(linkpath, sizeof(linkpath), "%s/%s", proc_path, de->d_name);
            ssize_t len = readlink(linkpath, target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                if (strstr(target, "original.txt") || strstr(target, "(deleted)")) {
                    found_fd = atoi(de->d_name);
                    break;
                }
            }
        }
        closedir(fddir);
    }

    if (found_fd >= 0) {
        snprintf(content_path, sizeof(content_path),
                 "/proc/%d/fd/%d", (int)bg_pid, found_fd);

        printf("   Content still being written (last lines via %s):\n", content_path);
        FILE *f = fopen(content_path, "r");
        if (f) {
            char line[128];
            /* go near the end */
            fseek(f, -200, SEEK_END);
            while (fgets(line, sizeof(line), f))
                fputs(line, stdout);
            fclose(f);
        }

        struct stat st2;
        if (stat(content_path, &st2) == 0) {
            printf("\n   Current size of the nameless file: %ld bytes\n",
                   (long)st2.st_size);
            printf("   (inode %lu is still alive, link count = %lu)\n",
                   (unsigned long)st2.st_ino, (unsigned long)st2.st_nlink);
        }
    } else {
        printf("   Could not locate the open fd (unexpected).\n");
    }

    printf("\n   The space is still accounted for by the filesystem\n");
    printf("   because the inode reference count has not reached zero.\n\n");

    /* ---------------------------------------------------------------
     * 6. Terminate the background process → space is released
     * --------------------------------------------------------------- */
    printf("6. Killing background writer (PID %d)...\n", (int)bg_pid);
    kill(bg_pid, SIGTERM);
    waitpid(bg_pid, NULL, 0);
    sleep(1);

    printf("   Process exited. The last reference to the inode is gone.\n");
    printf("   The data blocks have now been freed by the kernel.\n\n");

    /* Cleanup */
    printf("Cleanup: removing temporary directory\n");
    chdir("/");
    rmdir(workdir);

    printf("\nDone.\n");
    return 0;
}
