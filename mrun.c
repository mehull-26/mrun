#define _GNU_SOURCE

#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define STACK_SIZE (1024 * 1024)

struct child_config {
    char *rootfs;
    char **argv;
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int pivot_into_rootfs(const char *rootfs) {
    char oldroot[4096];

    /*
     * Make mount propagation private.
     * Otherwise mount/unmount events can leak between host and container.
     */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        die("mount MS_PRIVATE");
    }

    /*
     * pivot_root requires new_root to be a mount point.
     * Bind-mounting rootfs onto itself makes it a mount point.
     */
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
        die("bind mount rootfs");
    }

    snprintf(oldroot, sizeof(oldroot), "%s/.oldroot", rootfs);

    if (mkdir(oldroot, 0755) < 0 && errno != EEXIST) {
        die("mkdir oldroot");
    }

    /*
     * syscall because glibc does not expose pivot_root() wrapper everywhere.
     */
    if (syscall(SYS_pivot_root, rootfs, oldroot) < 0) {
        die("pivot_root");
    }

    if (chdir("/") < 0) {
        die("chdir /");
    }

    /*
     * After pivot_root:
     * new root is /
     * old host root is /.oldroot
     */
    if (umount2("/.oldroot", MNT_DETACH) < 0) {
        die("umount oldroot");
    }

    if (rmdir("/.oldroot") < 0) {
        die("rmdir oldroot");
    }

    return 0;
}

static int mount_proc(void) {
    if (mkdir("/proc", 0555) < 0 && errno != EEXIST) {
        die("mkdir /proc");
    }

    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        die("mount proc");
    }

    return 0;
}

static int bring_loopback_up(void) {
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        die("socket for loopback");
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        close(fd);
        die("ioctl get lo flags");
    }

    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        close(fd);
        die("ioctl set lo up");
    }

    close(fd);
    return 0;
}

static int child_main(void *arg) {
    struct child_config *config = arg;

    if (sethostname("mcontainer", strlen("mcontainer")) < 0) {
        die("sethostname");
    }

    if (pivot_into_rootfs(config->rootfs) < 0) {
        die("pivot_into_rootfs");
    }

    if (mount_proc() < 0) {
        die("mount_proc");
    }

    /*
     * Because we created a fresh network namespace,
     * even loopback starts down.
     */
    if (bring_loopback_up() < 0) {
        die("bring_loopback_up");
    }

    execvp(config->argv[0], config->argv);

    die("execvp");
    return 1;
}

int main(int argc, char **argv) {
    char *stack;
    char *stack_top;
    pid_t child_pid;
    int status;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <rootfs> <command> [args...]\n", argv[0]);
        fprintf(stderr, "Example: sudo %s ./rootfs /bin/sh\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct child_config config = {
        .rootfs = argv[1],
        .argv = &argv[2],
    };

    stack = malloc(STACK_SIZE);
    if (!stack) {
        die("malloc stack");
    }

    stack_top = stack + STACK_SIZE;

    int flags =
        CLONE_NEWUTS |
        CLONE_NEWPID |
        CLONE_NEWNS  |
        CLONE_NEWIPC |
        CLONE_NEWNET |
        SIGCHLD;

    child_pid = clone(child_main, stack_top, flags, &config);
    if (child_pid < 0) {
        die("clone");
    }

    printf("container init pid on host: %d\n", child_pid);

    if (waitpid(child_pid, &status, 0) < 0) {
        die("waitpid");
    }

    free(stack);

    if (WIFEXITED(status)) {
        printf("container exited with status: %d\n", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        printf("container killed by signal: %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return EXIT_FAILURE;
}
