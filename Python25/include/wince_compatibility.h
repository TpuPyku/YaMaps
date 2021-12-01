/*
 * Provides functions that are available on Windows generally but
 * not on Windows CE.
 * Some of the declarations are taken from the public domain MinGW headers.
 */

#ifndef WINCE_COMPATIBILITY_H
#define WINCE_COMPATIBILITY_H

/* FIXME: this file is included before pyport.h so we can't use PyAPI_FUNC() */
#ifdef Py_BUILD_CORE
#	define WINCE_PyAPI_FUNC(RTYPE) __declspec(dllexport) RTYPE
#	define WINCE_PyAPI_DATA(RTYPE) extern __declspec(dllexport) RTYPE
#else
#	define WINCE_PyAPI_FUNC(RTYPE) __declspec(dllimport) RTYPE
#	define WINCE_PyAPI_DATA(RTYPE) extern __declspec(dllimport) RTYPE
#endif

/* errno emulation */

#define ENOENT   2 /* ERROR_FILE_NOT_FOUND */
#define EACCES   5 /* ERROR_ACCESS_DENIED */
#define EBADF    6 /* ERROR_INVALID_HANDLE */
#define ENOMEM   8 /* ERROR_NOT_ENOUGH_MEMORY */
#define EEXIST 183 /* ERROR_ALREADY_EXISTS */
#define EINVAL  87 /* ERROR_INVALID_PARAMETER */

/* These values are never returned by APIs but may be set by Python */
#define EDOM    900
#define ERANGE  901
#define EINTR   902
#define ENXIO   903
#define EISDIR  904

/*
 * Because we need a per-thread errno, we define a function
 * pointer that we can call to return a pointer to the errno
 * for the current thread.  Then we define a macro for errno
 * that dereferences this function's result.
 *
 * This makes it syntactically just like the "real" errno.
 *
 * Using a function pointer allows us to use a very fast
 * function when there are no threads running and a slower
 * function when there are multiple threads running.
 */
void wince_errno_new_thread(int *errno_pointer);
void wince_errno_thread_exit(void);
int *(*wince_errno_pointer_function)(void);
#define	errno (*(*wince_errno_pointer_function)())

extern int _sys_nerr;
extern const char *_sys_errlist[];


/* Math */

/*
 * ldexp() DOES NOT return HUGE_VAL on overflow.  It returns 1.#INF,
 * so we need to define Py_HUGE_VAL in a way that allows us to correctly
 * detect overflow (this is tested by test.test_builtin)
 */
extern unsigned char wince_positive_double_infinity[];
#define	Py_HUGE_VAL (*(double *)wince_positive_double_infinity)


/* Time functions */

#define _TM_DEFINED

struct tm
{
	int	tm_sec;		/* Seconds: 0-59 */
	int	tm_min;		/* Minutes: 0-59 */
	int	tm_hour;	/* Hours since midnight: 0-23 */
	int	tm_mday;	/* Day of the month: 1-31 */
	int	tm_mon;		/* Months *since* january: 0-11 */
	int	tm_year;	/* Years since 1900 */
	int	tm_wday;	/* Days since Sunday (0-6) */
	int	tm_yday;	/* Days since Jan. 1: 0-365 */
	int	tm_isdst;	/* +1 Daylight Savings Time, 0 No DST,
				 * -1 don't know */
};
typedef long clock_t;

clock_t clock(void);

struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timep);
time_t mktime(struct tm *tm);
time_t time(time_t *TimeP);

extern char *tzname[2];
extern long timezone;
extern int daylight;

void tzset(void);


/* File & directory APIs */

#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2
#define FILE_TYPE_PIPE 3
#define FILE_TYPE_REMOTE 0x8000

#define GetFileType(h) (FILE_TYPE_DISK)

#define	_O_RDWR		(1<<0)
#define	_O_RDONLY	(2<<0)
#define	_O_WRONLY	(3<<0)
#define	_O_MODE_MASK	(3<<0)
#define	_O_TRUNC	(1<<2)
#define	_O_EXCL		(1<<3)
#define	_O_CREAT	(1<<4)

#define	O_RDWR _O_RDWR
#define	O_RDONLY _O_RDONLY
#define	O_WRONLY _O_WRONLY
#define	O_MODE_MASK _O_MODE_MASK
#define	O_TRUNC _O_TRUNC
#define	O_EXCL _O_EXCL
#define	O_CREAT _O_CREAT
#define O_BINARY _O_BINARY

#define fopen wince_fopen
WINCE_PyAPI_FUNC(FILE *) wince_fopen(const char *filename, const char *mode);
#define _wfopen wince_wfopen
WINCE_PyAPI_FUNC(FILE *) wince_wfopen(const wchar_t *filename, const wchar_t *mode);


int _wopen(const wchar_t *filename, int oflag, ...);
int _open(const char *filename, int oflag, ...);
FILE *_fdopen(int handle, const char *mode);
int _close(int handle);
int _write(int handle, const void *buffer, unsigned int count);
int _read(int handle, void *buffer, unsigned int count);
long _lseek(int handle, long offset, int origin);
__int64 _lseeki64(int handle, __int64 offset, int origin);
int _commit(int handle);
#define _open_osfhandle(h, m) (h)

#define open _open
#undef fdopen /* defined in Modules/zlib/zutil.h */
#define fdopen _fdopen
#define close _close
#define write _write
#define read _read
#define lseek _lseek

#define	rewind(f) fseek((f), 0, SEEK_SET)

/* These STDIO internal constants are used by fileobject */
#define	_IOFBF 0
#define	_IONBF 4
#define	_IOLBF 64

#define _get_osfhandle(fd) (fd)

int _unlink(const char *path);
#define unlink _unlink

#define	_S_IFIFO	0x1000	/* FIFO */
#define	_S_IFCHR	0x2000	/* Character */
#define	_S_IFBLK	0x3000	/* Block */
#define	_S_IFDIR	0x4000	/* Directory */
#define	_S_IFREG	0x8000	/* Regular */

#define	_S_IFMT		0xF000	/* File type mask */

#define	_S_IEXEC	0x0040
#define	_S_IWRITE	0x0080
#define	_S_IREAD	0x0100

#define	_S_IRWXU	(_S_IREAD | _S_IWRITE | _S_IEXEC)
#define	_S_IXUSR	_S_IEXEC
#define	_S_IWUSR	_S_IWRITE
#define	_S_IRUSR	_S_IREAD

#define	_S_ISDIR(m)	(((m) & _S_IFMT) == _S_IFDIR)
#define	_S_ISFIFO(m)	(((m) & _S_IFMT) == _S_IFIFO)
#define	_S_ISCHR(m)	(((m) & _S_IFMT) == _S_IFCHR)
#define	_S_ISBLK(m)	(((m) & _S_IFMT) == _S_IFBLK)
#define	_S_ISREG(m)	(((m) & _S_IFMT) == _S_IFREG)

#define	S_IFIFO		_S_IFIFO
#define	S_IFCHR		_S_IFCHR
#define	S_IFBLK		_S_IFBLK
#define	S_IFDIR		_S_IFDIR
#define	S_IFREG		_S_IFREG
#define	S_IFMT		_S_IFMT
#define	S_IEXEC		_S_IEXEC
#define	S_IWRITE	_S_IWRITE
#define	S_IREAD		_S_IREAD
#define	S_IRWXU		_S_IRWXU
#define	S_IXUSR		_S_IXUSR
#define	S_IWUSR		_S_IWUSR
#define	S_IRUSR		_S_IRUSR

#define	S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define	S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define	S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define	S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define	S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)

typedef unsigned int _dev_t;
typedef short _ino_t;
typedef unsigned short _mode_t;
typedef long _off_t;

struct _stat
{
	_dev_t	st_dev;		/* Equivalent to drive number 0=A 1=B ... */
	_ino_t	st_ino;		/* Always zero ? */
	_mode_t	st_mode;	/* See above constants */
	short	st_nlink;	/* Number of links. */
	short	st_uid;		/* User: Maybe significant on NT ? */
	short	st_gid;		/* Group: Ditto */
	_dev_t	st_rdev;	/* Seems useless (not even filled in) */
	_off_t	st_size;	/* File size in bytes */
	time_t	st_atime;	/* Accessed date (always 00:00 hrs local
				 * on FAT) */
	time_t	st_mtime;	/* Modified time */
	time_t	st_ctime;	/* Creation time */
};

int _fstat(int handle, struct _stat *buffer);
int _stat(const char *path, struct _stat *buffer);
int _wstat(const wchar_t *path, struct _stat *buffer);

#define fstat _fstat
#define stat _stat

/* Current directory APIs */

char* _getcwd (char*, int);
#define getcwd _getcwd
wchar_t* _wgetcwd(wchar_t *buffer, int maxlen);

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned long DWORD;
typedef long LONG;

DWORD GetCurrentDirectoryA(DWORD numbuf, char *buffer);
DWORD GetCurrentDirectoryW(DWORD numbuf, wchar_t *buffer);
WINCE_PyAPI_FUNC(int) SetCurrentDirectoryW(const wchar_t *path);
int SetCurrentDirectoryA(const char *path);

DWORD GetFullPathNameA(const char *path, DWORD num_buf, char *buf, char **file_part);
DWORD GetFullPathNameW(const wchar_t *path, DWORD num_buf, wchar_t *buf, wchar_t **file_part);

/* String functions */

size_t strxfrm(char *dest, char *src, size_t n);

#define wcscoll(s1, s2) wcscmp(s1, s2)
#define strcoll(s1, s2) strcmp(s1, s2)
#define stricmp _stricmp

#define strnicmp _strnicmp

/* Locale functions */

#define _LCONV_DEFINED

struct lconv
{
	char*	decimal_point;
	char*	thousands_sep;
	char*	grouping;
	char*	int_curr_symbol;
	char*	currency_symbol;
	char*	mon_decimal_point;
	char*	mon_thousands_sep;
	char*	mon_grouping;
	char*	positive_sign;
	char*	negative_sign;
	char	int_frac_digits;
	char	frac_digits;
	char	p_cs_precedes;
	char	p_sep_by_space;
	char	n_cs_precedes;
	char	n_sep_by_space;
	char	p_sign_posn;
	char	n_sign_posn;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);
int GetLocaleInfoA(DWORD lcid, DWORD lctype, char *buf, int buf_size);

/* Miscellaneous APIs */

#define abort() _exit(3)

/*
 * Make the character classification function call different so we
 * can compensate for Windows CE misclassification of EOF
 */
WINCE_PyAPI_FUNC(int) wince_isctype(int ch, int classification);
#define	_isctype	wince_isctype


/* 
 * These two functions are only used in sysmodule.c but will never
 * be called because isatty() always returns false
 */
#define	GetConsoleCP() (0)
#define	GetConsoleOutputCP() (0)

#define SetConsoleCtrlHandler(h, a) /* dummy */

/* Signals are not supported, so do nothing and succeed */
#define	signal(num, handler) (0)
#define SIG_ERR ((void *)(-1))
#define SIG_DFL ((void *)(0))
#define SIG_IGN ((void *)(1))

#define SIGINT 0

/* Environment variables are not supported */
#define GetEnvironmentVariable(name, buf, size) (0)
/* When setting environment variables, do nothing and succeed */
#define SetEnvironmentVariableA(n, v) (TRUE)
#define SetEnvironmentVariableW(n, v) (TRUE)

int _getpid(void);
#define getpid _getpid

typedef struct HINSTANCE__ *HINSTANCE;
typedef struct HWND__ *HWND;
typedef struct HKEY__ *HKEY;

HINSTANCE ShellExecuteA(HWND hwnd, const char *operation, const char *file, const char *params, const char *dir, int show_cmd);
HINSTANCE ShellExecuteW(HWND hwnd, const wchar_t *operation, const wchar_t *file, const wchar_t *params, const wchar_t *dir, int show_cmd);

typedef HINSTANCE HMODULE;

HMODULE LoadLibraryExA(const char *filename, void *reserved, DWORD flags);

DWORD FormatMessageA(DWORD flags, const void *source, DWORD msg, DWORD lang, char *buf, DWORD buf_size, va_list *args);

void OutputDebugStringA(const char *message);

LONG RegQueryValueExA(HKEY hkey, const char *value_name, DWORD *reserved, DWORD *type, BYTE *data, DWORD *num_data);

#define RegCreateKey(hKey, lpSubKey, phkResult) RegCreateKeyEx(hKey, lpSubKey, 0, NULL, 0, 0, NULL, phkResult, NULL)
#define RegQueryValueA(hKey, lpSubKey, lpValue, lpcbValue) RegQueryValueExA(hKey,lpSubKey, NULL, NULL, lpValue, lpcbValue)
#define RegQueryValueW(hKey, lpSubKey, lpValue, lpcbValue) RegQueryValueExW(hKey,lpSubKey, NULL, NULL, lpValue, lpcbValue)
#define RegQueryValue RegQueryValueW
#define RegSetValue(hKey, lpSubKey, dwType, lpData, cbData) RegSetValueEx(hKey, lpSubKey, 0, dwType, lpData, cbData)

typedef struct _WIN32_FIND_DATAA WIN32_FIND_DATAA;
typedef struct _WIN32_FIND_DATAW WIN32_FIND_DATAW;
typedef void *HANDLE;

HANDLE wince_FindFirstFileW(const wchar_t *filename, WIN32_FIND_DATAW *data);
#define FindFirstFileW wince_FindFirstFileW
HANDLE FindFirstFileA(const char *filename, WIN32_FIND_DATAA *data);

int FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *data);

HANDLE wince_CreateFileW(const wchar_t *filename, DWORD dwDesiredAccess, DWORD dwShareMode, struct _SECURITY_ATTRIBUTES *lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
#define CreateFileW wince_CreateFileW
HANDLE CreateFileA(const char *filename, DWORD dwDesiredAccess, DWORD dwShareMode, struct _SECURITY_ATTRIBUTES *lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

DWORD wince_GetFileAttributesW(const wchar_t *filename);
#define GetFileAttributesW wince_GetFileAttributesW
DWORD GetFileAttributesA(const char *filename);

BOOL wince_GetFileAttributesExW(const wchar_t *filename, enum _GET_FILEEX_INFO_LEVELS fInfoLevelId, void *lpFileInformation);
#define GetFileAttributesExW wince_GetFileAttributesExW
BOOL GetFileAttributesExA(const char *filename, enum _GET_FILEEX_INFO_LEVELS fInfoLevelId, void *lpFileInformation);

BOOL wince_SetFileAttributesW(const wchar_t *filename, DWORD attr);
#define SetFileAttributesW wince_SetFileAttributesW
BOOL SetFileAttributesA(const char *filename, DWORD attr);

BOOL wince_CreateDirectoryW(const wchar_t *path, struct _SECURITY_ATTRIBUTES *security);
#define CreateDirectoryW wince_CreateDirectoryW
BOOL CreateDirectoryA(const char *path, struct _SECURITY_ATTRIBUTES *security);

BOOL wince_RemoveDirectoryW(const wchar_t *path);
#define RemoveDirectoryW wince_RemoveDirectoryW
BOOL RemoveDirectoryA(const char *path);

BOOL wince_MoveFileW(const wchar_t *oldpath, const wchar_t *newpath);
#define MoveFileW wince_MoveFileW
BOOL MoveFileA(const char *oldpath, const char *newpath);

BOOL wince_DeleteFileW(const wchar_t *path);
#define DeleteFileW wince_DeleteFileW
BOOL DeleteFileA(const char *path);

char *CharPrevA(const char *start, const char *current);


typedef struct _object PyObject;

char* wince_readline(PyObject *fout, char* prompt);

#endif /* WINCE_COMPATIBILITY_H */
