#ifndef SOCKET_OS_h
#define SOCKET_OS_h

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif



//////////////poll//////////////
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)

#include "wepoll.h"
#include <winsock2.h>
#include <ws2tcpip.h>

struct event {
	void* s;
	bool read;
	bool write;
	bool error;
	bool eof;
};
typedef HANDLE poll_fd;
extern bool sp_invalid(poll_fd fd);
extern poll_fd sp_create();
extern void sp_release(poll_fd fd);
extern int sp_add(poll_fd fd, SOCKET sock, void* ud);
extern void sp_del(poll_fd fd, SOCKET sock);
extern void sp_write(poll_fd, SOCKET sock, void* ud, bool enable);
extern int sp_wait(poll_fd, struct event* e, int max);
extern void sp_nonblocking(SOCKET sock);

#else

struct event {
	void* s;
	bool read;
	bool write;
	bool error;
	bool eof;
};
typedef int poll_fd;

extern bool sp_invalid(poll_fd fd);
extern poll_fd sp_create();
extern void sp_release(poll_fd fd);
extern int sp_add(poll_fd fd, int sock, void* ud);
extern void sp_del(poll_fd fd, int sock);
extern void sp_write(poll_fd, int sock, void* ud, bool enable);
extern int sp_wait(poll_fd, struct event* e, int max);
extern void sp_nonblocking(int sock);

#endif

// ////////////poll//////////////





// ////////////atomic//////////////
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)

static inline bool __sync_bool_compare_and_swap(long* ptr, int oval, int nval) {
	return oval == InterlockedCompareExchange(ptr, nval, oval) ? 1 : 0;
}

static inline long __sync_add_and_fetch(long* ptr, long n) {
	InterlockedAdd(ptr, n);
	return *ptr;
}

static inline long __sync_sub_and_fetch(long* ptr, long n) {
	InterlockedAdd(ptr, -n);
	return *ptr;
}

static inline long __sync_and_and_fetch(long* ptr, long n) {
	InterlockedAnd(ptr, n);
	return *ptr;
}

#define ATOM_CAS(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS_POINTER(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
// #define ATOM_FINC(ptr) __sync_fetch_and_add(ptr, 1)
// #define ATOM_FDEC(ptr) __sync_fetch_and_sub(ptr, 1)
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)
#define ATOM_DEC(ptr) __sync_sub_and_fetch(ptr, 1)
#define ATOM_ADD(ptr,n) __sync_add_and_fetch(ptr, n)
#define ATOM_SUB(ptr,n) __sync_sub_and_fetch(ptr, n)
#define ATOM_AND(ptr,n) __sync_and_and_fetch(ptr, n)

#else

#define ATOM_CAS(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS_POINTER(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)
#define ATOM_FINC(ptr) __sync_fetch_and_add(ptr, 1)
#define ATOM_DEC(ptr) __sync_sub_and_fetch(ptr, 1)
#define ATOM_FDEC(ptr) __sync_fetch_and_sub(ptr, 1)
#define ATOM_ADD(ptr,n) __sync_add_and_fetch(ptr, n)
#define ATOM_SUB(ptr,n) __sync_sub_and_fetch(ptr, n)
#define ATOM_AND(ptr,n) __sync_and_and_fetch(ptr, n)

#endif
// ////////////atomic//////////////


//////////////spinlock//////////////

#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)

#include <windows.h>
#include <assert.h>

struct spinlock {
    CRITICAL_SECTION lock;
};

static inline void spinlock_init(struct spinlock *lock) {
	BOOL ret = InitializeCriticalSectionAndSpinCount(&lock->lock, 4000);
	if (!ret)
	{
		assert(false);
	}
}

static inline void spinlock_lock(struct spinlock *lock) {
	EnterCriticalSection(&lock->lock);
}

static inline int spinlock_trylock(struct spinlock *lock) {
	return TryEnterCriticalSection(&lock->lock);
}

static inline void spinlock_unlock(struct spinlock *lock) {
    LeaveCriticalSection(&lock->lock);
}

static inline void spinlock_destroy(struct spinlock *lock) {
	DeleteCriticalSection(&lock->lock);
}

#else

#ifndef USE_PTHREAD_LOCK

struct spinlock {
	int lock;
};

static inline void spinlock_init(struct spinlock *lock) {
	lock->lock = 0;
}

static inline void spinlock_lock(struct spinlock *lock) {
	while (__sync_lock_test_and_set(&lock->lock,1)) {}
}

static inline int spinlock_trylock(struct spinlock *lock) {
	return __sync_lock_test_and_set(&lock->lock,1) == 0;
}

static inline void spinlock_unlock(struct spinlock *lock) {
	__sync_lock_release(&lock->lock);
}

static inline void spinlock_destroy(struct spinlock *lock) {
	(void) lock;
}

#else

#include <pthread.h>

struct spinlock {
	pthread_mutex_t lock;
};

static inline void spinlock_init(struct spinlock *lock) {
	pthread_mutex_init(&lock->lock, NULL);
}

static inline void spinlock_lock(struct spinlock *lock) {
	pthread_mutex_lock(&lock->lock);
}

static inline int spinlock_trylock(struct spinlock *lock) {
	return pthread_mutex_trylock(&lock->lock) == 0;
}

static inline void spinlock_unlock(struct spinlock *lock) {
	pthread_mutex_unlock(&lock->lock);
}

static inline void spinlock_destroy(struct spinlock *lock) {
	pthread_mutex_destroy(&lock->lock);
}

#endif

#endif

//////////////spinlock//////////////




// ////////////socket//////////////
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h> /* must always be included before ws2tcpip.h */
#include <ws2tcpip.h> /* for struct sock_addr used in zookeeper.h */
#undef near

int socket_write(int fd, const void* buffer, size_t sz);
int socket_read(int fd, void* buffer, size_t sz);
int socket_close(int fd);
int socket_connect(SOCKET s, const struct sockaddr* name, int namelen);
int socket_send(SOCKET s, const char* buffer, int sz, int flag);
int socket_recv(SOCKET s, char* buffer, int sz, int flag);

int socket_recvfrom(SOCKET s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen);
int socket_start();
int socket_stop();

int socket_getsockopt(SOCKET s, int level, int optname, void* optval, int* optlen);
int socket_setsockopt(SOCKET s, int level, int optname, const void* optval, int optlen);
int socket_pipe(int fds[2]);

#else

#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
//static inline int socket_write(int fd, const void* buffer, size_t sz) {
//	return write(fd, buffer, sz);
//}
//
//static inline int socket_read(int fd, void* buffer, size_t sz) {
//	return read(fd, buffer, sz);
//}

static inline int socket_close(int fd) {
	return close(fd);
}

//static inline int socket_connect(int s, const struct sockaddr* name, int namelen) {
//	return connect(s, name, namelen);
//}
//
//static inline int socket_send(int s, const char* buffer, int sz, int flag) {
//	return send(s, buffer, sz, flag);
//}
//
//static inline int socket_recv(int s, char* buffer, int sz, int flag) {
//	return recv(s, buffer, sz, flag);
//}
//
//static inline int socket_recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, socklen_t* fromlen) {
//	return recvfrom(s, buf, len, flags, from, fromlen);
//}
#define socket_write write
#define socket_read read
//#define socket_close close

#define socket_connect connect
#define socket_send send
#define socket_recv recv
#define socket_recvfrom recvfrom

#define socket_getsockopt getsockopt
#define socket_setsockopt setsockopt

#define socket_pipe pipe
//int socket_pipe(int fds[2]);

inline int socket_start() { return 0; }
inline int socket_stop() { return 0; }

#endif

// ////////////socket//////////////




#ifdef __cplusplus
}
#endif

#endif //SOCKET_OS_h
