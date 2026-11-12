/* The Windows side of walkman.c: what win32/sys/ioctl.h promises.
 *
 * The game's source stays as it is. It asks for the terminal's size with
 * ioctl, sets the keyboard up with stty, reads keys with read, and plays
 * /tmp/walkman-*.wav by forking afplay or aplay. Each of those lands here.
 *
 * It works in a console (Windows Terminal, cmd, PowerShell, or mintty with
 * ConPTY), and in an MSYS2 pty (mintty without ConPTY), where this program
 * sees pipes instead of a console and stty still does the work.
 */
#define WALKMAN_COMPAT_C
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>

/* ----------------------------------------------------------------- terminal */

static HANDLE in, out;
static DWORD in_mode, out_mode;
static int in_console, out_console;

static void restore(void) {
	fflush(stdout);
	if (in_console) SetConsoleMode(in, in_mode);
	if (out_console) SetConsoleMode(out, out_mode);
	timeEndPeriod(1);
}

/* Ctrl+Break, or the window closing: leave the console the way it was. */
static BOOL WINAPI closing(DWORD event) {
	(void)event;
	if (in_console) SetConsoleMode(in, in_mode);
	if (out_console) SetConsoleMode(out, out_mode);
	return FALSE;
}

/* Runs once, from the first thing main does (its ioctl). */
static void init(void) {
	static int ready;
	if (ready) return;
	ready = 1;
	in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
	in_console = GetConsoleMode(in, &in_mode), out_console = GetConsoleMode(out, &out_mode);
	if (out_console) {
		// Escapes, and a full row that doesn't wrap before its newline.
		DWORD vt = out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (!SetConsoleMode(out, vt | DISABLE_NEWLINE_AUTO_RETURN)) SetConsoleMode(out, vt);
	} else
		_setmode(1, _O_BINARY);  // a pipe or a pty: no \r before \n
	timeBeginPeriod(1);  // so usleep can keep 60 frames a second
	SetConsoleCtrlHandler(closing, TRUE);
	atexit(restore);
}

int wm_ioctl(int fd, int request, struct winsize *ws) {
	CONSOLE_SCREEN_BUFFER_INFO info;
	FILE *p;
	int rows, cols, ok = 0;
	(void)fd;
	init();
	if (request != TIOCGWINSZ) return -1;
	if (GetConsoleScreenBufferInfo(out, &info)) {
		ws->ws_col = info.srWindow.Right - info.srWindow.Left + 1;
		ws->ws_row = info.srWindow.Bottom - info.srWindow.Top + 1;
		return 0;
	}
	// No console. In an MSYS2 pty, stty knows the size.
	if (GetFileType(in) == FILE_TYPE_PIPE && (p = _popen("stty size 2>NUL", "r"))) {
		if ((ok = fscanf(p, "%d %d", &rows, &cols) == 2)) ws->ws_row = rows, ws->ws_col = cols;
		_pclose(p);
	}
	return ok - 1;
}

/* The game only runs stty: "-icanon -echo min 0 time 0" to get every key as
 * it's pressed, "icanon echo" to put things back. In a console that's a mode
 * (with Ctrl+C as a key: wm_read raises SIGINT); in a pty, stty itself. */
int wm_system(const char *command) {
	init();
	if (strncmp(command, "stty", 4)) return system(command);
	if (in_console) {
		DWORD raw = in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
		return SetConsoleMode(in, strstr(command, "-icanon") ? raw : in_mode) ? 0 : -1;
	}
	return GetFileType(in) == FILE_TYPE_PIPE ? system(command) : -1;
}

/* Keys typed since the last call, without waiting. The up arrow is 'A', the
 * last byte of the escape a terminal sends for it, which the game looks for. */
int wm_read(int fd, void *buf, unsigned n) {
	char *b = buf, c;
	INPUT_RECORD e;
	DWORD k, got = 0;
	if (fd) return _read(fd, buf, n);
	init();
	if (!in_console) {  // a pty's pipe: bytes, as on any Unix
		if (PeekNamedPipe(in, 0, 0, 0, &k, 0) && k) ReadFile(in, buf, k < n ? k : n, &got, 0);
		return got;
	}
	while (got < n && GetNumberOfConsoleInputEvents(in, &k) && k && ReadConsoleInputA(in, &e, 1, &k)) {
		if (e.EventType != KEY_EVENT || !e.Event.KeyEvent.bKeyDown) continue;
		c = e.Event.KeyEvent.wVirtualKeyCode == VK_UP ? 'A' : e.Event.KeyEvent.uChar.AsciiChar;
		if (c == 3) raise(SIGINT);
		if (c) b[got++] = c;
	}
	return got;
}

void (*wm_signal(int sig, void (*handler)(int)))(int) { return sig == SIGCHLD ? SIG_DFL : signal(sig, handler); }

/* A line-buffered stream with no size makes the Universal CRT abort, and
 * Windows has no line buffering anyway. */
int wm_setvbuf(FILE *f, char *buf, int mode, size_t size) { return setvbuf(f, buf, size < 2 ? _IONBF : mode, size); }

/* Only ever subtracted from another one, so counting from when the computer
 * started is fine, and the performance counter is much finer than the clock. */
int wm_gettimeofday(struct timeval *t) {
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f), QueryPerformanceCounter(&c);
	t->tv_sec = c.QuadPart / f.QuadPart, t->tv_usec = c.QuadPart % f.QuadPart * 1000000 / f.QuadPart;
	return 0;
}

/* -------------------------------------------------------------------- files */

/* "/tmp/x" is x in the user's temporary folder. */
static const char *native(const char *path, char *buf) {
	DWORD n;
	if (strncmp(path, "/tmp/", 5) || !(n = GetTempPathA(MAX_PATH, buf)) || n > MAX_PATH) return path;
	snprintf(buf + n, 64, "%s", path + 5);
	return buf;
}

FILE *wm_fopen(const char *path, const char *mode) {
	char buf[MAX_PATH + 64];
	return fopen(native(path, buf), mode);
}

/* -------------------------------------------------------------------- sound */

/* A few sounds at once, each on its own wave device (Windows mixes them): the
 * music and the chimes over it. A sound's "pid" is its number, counted from 1.
 * Opening a device takes a tenth of a second or more, a few frames of the
 * game. So the first sound, before the game starts, opens three of them, and
 * a device stays open when its sound ends, for the next one. */
enum { VOICES = 6 };
static struct {
	HWAVEOUT dev;
	WAVEFORMATEX fmt;
	WAVEHDR hdr;
	int pid;
} voice[VOICES];
static int pids;
jmp_buf wm_fork;

static void hush(int i) {
	if (!voice[i].pid) return;
	waveOutReset(voice[i].dev);
	waveOutUnprepareHeader(voice[i].dev, &voice[i].hdr, sizeof(WAVEHDR));
	free(voice[i].hdr.lpData);
	voice[i].pid = 0;
}

/* Opens voice i for sounds of this format, unless it already is. */
static int tune_in(int i, WAVEFORMATEX *fmt) {
	if (voice[i].dev && memcmp(fmt, &voice[i].fmt, sizeof *fmt)) waveOutClose(voice[i].dev), voice[i].dev = 0;
	if (!voice[i].dev && waveOutOpen(&voice[i].dev, WAVE_MAPPER, fmt, 0, 0, CALLBACK_NULL)) voice[i].dev = 0;
	voice[i].fmt = *fmt;
	return voice[i].dev != 0;
}

/* Starts a .wav the game saved (a 44-byte header, then the samples) and
 * returns its pid, or -1. */
static int play(const char *path) {
	char buf[MAX_PATH + 64], head[44], *data = 0;
	WAVEFORMATEX fmt = {0};
	FILE *f;
	long size = 0;
	int i, v = -1;
	if (!(f = fopen(native(path, buf), "rb"))) return -1;
	if (fread(head, 1, 44, f) == 44 && !memcmp(head, "RIFF", 4) && !fseek(f, 0, SEEK_END) && (size = ftell(f) - 44) > 0 &&
	    (data = malloc(size)) && !fseek(f, 44, SEEK_SET))
		size = fread(data, 1, size, f);
	fclose(f);
	if (!data || size <= 0) return free(data), -1;
	memcpy(&fmt, head + 20, 16);  // the "fmt " chunk is a WAVEFORMATEX without its last field
	for (i = 0; i < VOICES; i++) {
		if (voice[i].pid && voice[i].hdr.dwFlags & WHDR_DONE) hush(i);  // it finished
		if (!pids && i < 3) tune_in(i, &fmt);
		if (!voice[i].pid && (v < 0 || !voice[v].dev)) v = i;  // a free one, better if it's open
	}
	if (v < 0 || !tune_in(v, &fmt)) return free(data), -1;
	memset(&voice[v].hdr, 0, sizeof(WAVEHDR));
	voice[v].hdr.lpData = data, voice[v].hdr.dwBufferLength = size;
	waveOutPrepareHeader(voice[v].dev, &voice[v].hdr, sizeof(WAVEHDR));
	waveOutWrite(voice[v].dev, &voice[v].hdr, sizeof(WAVEHDR));
	return voice[v].pid = ++pids;
}

/* execlp("afplay", "afplay", path, 0): the last argument is the file. Plays
 * it, and returns from fork() (see ioctl.h) instead of from here. */
int wm_execlp(const char *file, const char *arg, ...) {
	const char *path = arg, *a;
	va_list ap;
	(void)file;
	va_start(ap, arg);
	while ((a = va_arg(ap, const char *))) path = a;
	va_end(ap);
	longjmp(wm_fork, play(path));
}

int wm_kill(int pid, int sig) {
	(void)sig;
	for (int i = 0; i < VOICES; i++)
		if (voice[i].pid == pid) hush(i);
	return 0;
}
