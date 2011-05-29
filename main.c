#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <magic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#ifdef DEBUG
#define dprintf(fmt, args...)					\
	do {							\
		fprintf(stderr, "%s:%d: " fmt,			\
			__FILE__, __LINE__, ##args);		\
	} while (0)
#else /* !defined(DEBUG) */
#define dprintf(fmt, args...)					\
	do {							\
	} while (0)
#endif /* !defined(DEBUG) */
/*
 * Ugly, passing over the head of things.
 */
static long pid_max = -1;
static magic_t magic_cookie;
static struct dirent *parent = NULL;

static int proc_filter(const struct dirent *dirent)
{
	char buf[256], path[256] = "/proc/", *endptr = NULL;
	ssize_t read_ret;
	long result;
	int fd;

	result = strtol(dirent->d_name, &endptr, 10);
	if (endptr == dirent->d_name || result < 0 || result > pid_max)
		return 0;
	strcat(path, dirent->d_name);
	strcat(path, "/cmdline");
	if ((fd = open(path, O_RDONLY)) < 0) {
		dprintf("Open failed for %s.\n", path);
		return 0;
	}
	read_ret = read(fd, buf, sizeof(buf));
	/* Very bad. read_ret == 0 needs some kind of retry. */
	if (read_ret < 0) {
		dprintf("Hard read failure for %s\n", path);
		return 0;
	} else if (!read_ret) {
		/* It's oddly typical to have /proc/$PID/cmdline empty. */
		if (0)
			dprintf("Transient read failure or empty cmdline for %s\n", path);
		return 0;
	}
	if (!strstr(buf, "plugin"))
		return 0;
	return 1;
}

static int fd_filter(const struct dirent *dirent)
{
	char path[256] = "/proc/", *endptr = NULL;
	const char *magic;
	long result;

	result = strtol(dirent->d_name, &endptr, 10);
	if (endptr == dirent->d_name || result < 0 || result > INT_MAX)
		return 0;
	strcat(path, parent->d_name);
	strcat(path, "/fd/");
	strcat(path, dirent->d_name);
	if (!(magic = magic_file(magic_cookie, path))) {
		dprintf("Unable to determine magic on %s\n", path);
		return 0;
	}
	return !strcmp(magic, "ISO Media, MPEG v4 system, version 2");
}

int get_pid_max(void)
{
	/* vastly in excess of what's known to reside in-kernel */
	char buf[256], *endptr = NULL;
	struct stat stat_buf;
	ssize_t read_ret;
	int fd = open("/proc/sys/kernel/pid_max", O_RDONLY);

	if (fd < 0) {
		dprintf("open() failed.\n");
		return -1;
	}
	read_ret = read(fd, buf, sizeof(buf));
	if (read_ret < 0) {
		dprintf("Hard read() failure.\n");
		return -1;
	} else if (!read_ret && errno != EAGAIN) {
		dprintf("Transient read() failure.\n");
		return -1;
	} else if (read_ret >= (signed)sizeof(buf)) {
		dprintf("Integer too high of precision for buffer.\n");
		return -1;
	}
	pid_max = strtol(buf, &endptr, 10);
	if (endptr == buf || endptr - buf < stat_buf.st_size
				|| pid_max <= 0 || errno) {
		dprintf("Malformatted numeral, endptr - buf = %zd.\n",
						endptr - buf);
		dprintf("read_ret = %zd\n", read_ret);
		dprintf("stat_buf.st_size = %ld\n", stat_buf.st_size);
		dprintf("buf = \"%s\"\n", buf);
		pid_max = -1;
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int i, nr_dirs, ret = EX_OK;
	struct dirent **proclist = NULL;
	char *tmp, *buf = NULL;

	if (argc != 2)
		return EX_USAGE;
	if (get_pid_max())
		return EX_OSERR;
	if (!(magic_cookie = magic_open(MAGIC_SYMLINK)))
		return EX_OSFILE;
	if (magic_load(magic_cookie, "/usr/share/misc/magic.mgc:/etc/magic"))
		return EX_OSFILE;
	if ((nr_dirs = scandir("/proc/", &proclist, proc_filter, NULL)) < 0)
		return EX_OSFILE;
	dprintf("nr_dirs = %d\n", nr_dirs);
	if (nr_dirs > 1)
		dprintf("Non-unique match for plugin process!\n");
	for (i = 0; i < nr_dirs; ++i) {
		struct dirent **fdlist = NULL;
		char path[256] = "/proc/";
		int nr_fds, j;

		parent = proclist[i];
		dprintf("proclist[%d] = %s\n", i, proclist[i]->d_name);
		strcat(path, proclist[i]->d_name);
		strcat(path, "/fd");
		if ((nr_fds = scandir(path, &fdlist, fd_filter, NULL)) < 0) {
			dprintf("scandir failure on %s\n", path);
			return EX_OSFILE;
		}
		for (j = 0; j < nr_fds; ++j)
			dprintf("%s->fd[%d] = %s\n", proclist[i]->d_name,
					j, fdlist[j]->d_name);
		if (nr_fds > 1) {
			dprintf("Non-unique match for MPEG fd!\n");
			return EX_OSFILE;
		} else {
			int in_fd, out_fd, size_same = 0, ret = EX_OK;
			size_t buf_size = 0, size_diff, left;
			struct stat in_stat, out_stat;
			strcat(path, "/");
			strcat(path, fdlist[0]->d_name);
			if ((in_fd = open(path, O_RDONLY)) < 0) {
				ret = EX_UNAVAILABLE;
				goto out;
			}
			if ((out_fd = open(argv[1], O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP)) < 0) {
				ret = EX_UNAVAILABLE;
				goto out;
			}
			while (size_same <= 2) {
				struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };

				if (fstat(out_fd, &out_stat))
					return EX_OSERR;
				if (fstat(in_fd, &in_stat))
					return EX_OSERR;
				if (in_stat.st_size == out_stat.st_size) {
					size_same++;
					goto sleep;
				}
				size_same = 0;
				if (out_stat.st_size > in_stat.st_size) {
sleep:
					nanosleep(&req, NULL);
					continue;
				}
				if (lseek(in_fd, out_stat.st_size, SEEK_SET) != out_stat.st_size) {
					ret = EX_OSERR;
					goto out_free;
				}
				if (lseek(out_fd, out_stat.st_size, SEEK_SET) != out_stat.st_size) {
					ret = EX_OSERR;
					goto out_free;
				}
				size_diff = in_stat.st_size - out_stat.st_size;
				if (buf_size < size_diff) {
					if (!(tmp = realloc(buf, size_diff))) {
						ret = EX_OSERR;
						goto out_free;
					}
					buf = tmp;
					buf_size = size_diff;
				}
				left = size_diff;
				while (left > 0) {
					ssize_t read_ret, write_ret;
					size_t write_left;

					read_ret = read(in_fd, buf, left);
					if (read_ret < 0) {
						ret = EX_OSERR;
						goto out_free;
					} else if (!read_ret) {
						if (errno && errno != EAGAIN) {
							ret = EX_OSERR;
							goto out_free;
						}
						goto sleep;
					}
					write_left = read_ret;
					while (write_left > 0) {
						write_ret = write(out_fd, buf, write_left);
						if (write_ret < 0) {
							ret = EX_OSERR;
							goto out_free;
						} else if (!write_ret) {
							if (errno && errno != EAGAIN) {
								ret = EX_OSERR;
								goto out_free;
							}
							goto sleep;
						}
						write_left -= write_ret;
					}
					left -= read_ret;
				}
			}
		}
	}
	return EX_OK;
out_free:
	free(buf);
out:
	return ret;
}
