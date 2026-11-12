/* <sys/ioctl.h> for Windows, so that walkman.c compiles unchanged with
 * MinGW-w64 (MSYS2's UCRT64, MINGW64 and CLANG64 shells):
 *
 *   cc -Iwin32 walkman.c win32/compat.c -o walkman -lm -lwinmm
 *
 * Windows has no ioctl, stty, fork, kill, SIGCHLD, /tmp or aplay. The game
 * includes this file where it expects the terminal's ioctl, and gets every
 * one of them: each is a macro here for a function in compat.c.
 */
#ifndef WALKMAN_WIN32_IOCTL_H
#define WALKMAN_WIN32_IOCTL_H

/* Everything the game includes after this file, so that the macros below
 * can't rename a declaration inside a system header. */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define TIOCGWINSZ 0x5413
#define SIGCHLD 17  // not a signal here: wm_signal leaves it alone

int wm_ioctl(int fd, int request, struct winsize *ws);
int wm_system(const char *command);
int wm_read(int fd, void *buf, unsigned n);
FILE *wm_fopen(const char *path, const char *mode);
int wm_setvbuf(FILE *f, char *buf, int mode, size_t size);
int wm_gettimeofday(struct timeval *t);
void (*wm_signal(int sig, void (*handler)(int)))(int);
int wm_execlp(const char *file, const char *arg, ...);
int wm_kill(int pid, int sig);
extern jmp_buf wm_fork;

#ifndef WALKMAN_COMPAT_C
#define ioctl wm_ioctl
#define system wm_system
#define read wm_read
#define fopen wm_fopen
#define setvbuf wm_setvbuf
#define gettimeofday(t, zone) wm_gettimeofday(t)
#define signal wm_signal
#define kill wm_kill
/* The game plays a sound by forking a child that execs a player. Here fork()
 * marks the spot and returns 0; the "child" reaches execlp, which starts the
 * sound inside this process and jumps back to the mark, so that fork() returns
 * a second time with the sound's number, as it would in the parent. */
#define fork() setjmp(wm_fork)
#define execlp wm_execlp
#endif

#endif
