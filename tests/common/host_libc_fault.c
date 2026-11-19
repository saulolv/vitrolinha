/**
 * @file host_libc_fault.c
 * @brief Implementação de @ref host_libc_fault.h.
 */

#define _GNU_SOURCE

#include "host_libc_fault.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static enum host_libc_fault armed;
static void (*read_hook)(void);

void host_libc_fault_arm(enum host_libc_fault fault)
{
	armed = fault;
}

void host_libc_fault_on_read(void (*hook)(void))
{
	read_hook = hook;
}

/** @brief Consome a falha armada, se for @p fault. */
static bool fire(enum host_libc_fault fault)
{
	if (armed != fault) {
		return false;
	}
	armed = HOST_LIBC_FAULT_NONE;
	return true;
}

/** @brief A definição seguinte de @p name, isto é, a da libc. */
static void *next(const char *name)
{
	return dlsym(RTLD_NEXT, name);
}

struct dirent *readdir(DIR *dir)
{
	struct dirent *(*real)(DIR *) = (struct dirent *(*)(DIR *))next("readdir");

	if (fire(HOST_LIBC_FAULT_READDIR)) {
		errno = EIO;
		return NULL;
	}
	return real(dir);
}

int fstatat(int dirfd, const char *path, struct stat *info, int flags)
{
	int (*real)(int, const char *, struct stat *, int) =
		(int (*)(int, const char *, struct stat *, int))next("fstatat");

	if (fire(HOST_LIBC_FAULT_FSTATAT)) {
		errno = EIO;
		return -1;
	}
	return real(dirfd, path, info, flags);
}

int fstat(int fd, struct stat *info)
{
	int (*real)(int, struct stat *) = (int (*)(int, struct stat *))next("fstat");

	if (fire(HOST_LIBC_FAULT_FSTAT)) {
		errno = EIO;
		return -1;
	}
	return real(fd, info);
}

ssize_t read(int fd, void *buf, size_t count)
{
	ssize_t (*real)(int, void *, size_t) = (ssize_t (*)(int, void *, size_t))next("read");
	void (*hook)(void) = read_hook;

	if (hook != NULL) {
		read_hook = NULL;
		hook();
	}
	if (fire(HOST_LIBC_FAULT_READ_EINTR)) {
		errno = EINTR;
		return -1;
	}
	if (fire(HOST_LIBC_FAULT_READ_EIO)) {
		errno = EIO;
		return -1;
	}
	if (fire(HOST_LIBC_FAULT_READ_EOF)) {
		return 0;
	}
	return real(fd, buf, count);
}
