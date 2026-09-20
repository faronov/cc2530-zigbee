/* SPDX-License-Identifier: BSD-3-Clause */
/* Test-only: installed by the synthetic library before OpenOCD's main(). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error This test confinement requires Linux x86-64.
#endif

#define DENY(n) \
	BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (n), 0, 1), \
	BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)

__attribute__((constructor))
static void confine(void)
{
	struct sock_filter code[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
		DENY(SYS_open), DENY(SYS_openat), DENY(SYS_openat2), DENY(SYS_creat),
		DENY(SYS_ioctl), DENY(SYS_socket), DENY(SYS_socketpair), DENY(SYS_connect),
		DENY(SYS_getdents), DENY(SYS_getdents64),
		DENY(SYS_execve), DENY(SYS_execveat),
		DENY(SYS_io_uring_setup), DENY(SYS_io_uring_enter), DENY(SYS_io_uring_register),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog program = {
		.len = (unsigned short)(sizeof(code) / sizeof(code[0])), .filter = code,
	};
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
			prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program)) {
		perror("test confinement");
		_exit(125);
	}
	static const char message[] = "NS51_FAKE_SANDBOX\n";
	if (write(STDERR_FILENO, message, sizeof(message) - 1) != sizeof(message) - 1)
		_exit(125);
}

#ifdef SANDBOX_SELFTEST
int main(void)
{
	const long numbers[] = {
		SYS_open, SYS_openat, SYS_openat2, SYS_creat, SYS_ioctl,
		SYS_socket, SYS_socketpair, SYS_connect, SYS_getdents, SYS_getdents64,
		SYS_execve, SYS_execveat, SYS_io_uring_setup, SYS_io_uring_enter,
		SYS_io_uring_register,
	};
	for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
		errno = 0;
		if (syscall(numbers[i], -1L, 0L, 0L, 0L, 0L, 0L) != -1 || errno != EPERM)
			return 1;
	}
	puts("15 denied syscall families");
	return 0;
}
#endif
