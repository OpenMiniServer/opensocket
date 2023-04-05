#include "socket_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//////////////poll//////////////

#if defined(__linux__)

#include <sys/types.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

bool sp_invalid(int efd) {
	return efd == -1;
}

int sp_create() {
	return epoll_create(1024);
}

void sp_release(int efd) {
	close(efd);
}

int sp_add(int efd, int sock, void* ud) {
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = ud;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) == -1) {
		return 1;
	}
	return 0;
}

void sp_del(int efd, int sock) {
	epoll_ctl(efd, EPOLL_CTL_DEL, sock, NULL);
}

void sp_write(int efd, int sock, void* ud, bool enable) {
	struct epoll_event ev;
	ev.events = EPOLLIN | (enable ? EPOLLOUT : 0);
	ev.data.ptr = ud;
	epoll_ctl(efd, EPOLL_CTL_MOD, sock, &ev);
}

int sp_wait(int efd, struct event* e, const int max) {
	struct epoll_event ev[max];
	int n = epoll_wait(efd, ev, max, -1);
	int i = 0;
	unsigned flag = 0;
	for (i = 0; i < n; ++i) {
		e[i].s = ev[i].data.ptr;
		flag = ev[i].events;
		e[i].write = (flag & EPOLLOUT) != 0;
		e[i].read = (flag & (EPOLLIN | EPOLLHUP)) != 0;
		e[i].error = (flag & EPOLLERR) != 0;
		e[i].eof = false;
	}
	return n;
}

void sp_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) {
		return;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}


#elif defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)

#define _CRT_SECURE_NO_WARNINGS
#include <sys/types.h>
#include <fcntl.h>
#define O_NONBLOCK 1
#define F_SETFL 0
#define F_GETFL 1

static inline int fcntl(SOCKET fd, int cmd, long arg) {
	if (cmd == F_GETFL) return 0;
	if (cmd == F_SETFL && arg == O_NONBLOCK) {
		u_long ulOption = 1;
		ioctlsocket(fd, FIONBIO, &ulOption);
	}
	return 1;
}

bool sp_invalid(poll_fd efd) {
	return efd == 0;
}

poll_fd sp_create() {
	return epoll_create(1024);
}

void sp_release(poll_fd efd) {
	epoll_close(efd);
}

int sp_add(poll_fd efd, SOCKET sock, void* ud) {
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = ud;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sock, &ev) == -1) {
		return 1;
	}
	return 0;
}

void sp_del(poll_fd efd, SOCKET sock) {
	epoll_ctl(efd, EPOLL_CTL_DEL, sock, NULL);
}

void sp_write(poll_fd efd, SOCKET sock, void* ud, bool enable) {
	struct epoll_event ev;
	ev.events = EPOLLIN | (enable ? EPOLLOUT : 0);
	ev.data.ptr = ud;
	epoll_ctl(efd, EPOLL_CTL_MOD, sock, &ev);
}

int sp_wait(poll_fd efd, struct event* e, const int max) {
	struct epoll_event* ev = (struct epoll_event*)malloc(sizeof(struct epoll_event) * max);
	if (!ev) return 0;
	int n = epoll_wait(efd, ev, max, -1);
	int i = 0;
	unsigned flag = 0;
	for (i = 0; i < n; ++i) {
		e[i].s = ev[i].data.ptr;
		flag = ev[i].events;
		e[i].write = (flag & EPOLLOUT) != 0;
		e[i].read = (flag & (EPOLLIN | EPOLLHUP)) != 0;
		e[i].error = (flag & EPOLLERR) != 0;
		e[i].eof = false;
	}
	free(ev);
	return n;
}

void sp_nonblocking(SOCKET fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) {
		return;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

bool sp_invalid(int kfd) {
	return kfd == -1;
}

int sp_create() {
	return kqueue();
}

void sp_release(int kfd) {
	close(kfd);
}

void sp_del(int kfd, int sock) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
	EV_SET(&ke, sock, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(kfd, &ke, 1, NULL, 0, NULL);
}

int sp_add(int kfd, int sock, void* ud) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_READ, EV_ADD, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1 || ke.flags & EV_ERROR) {
		return 1;
	}
	EV_SET(&ke, sock, EVFILT_WRITE, EV_ADD, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1 || ke.flags & EV_ERROR) {
		EV_SET(&ke, sock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		kevent(kfd, &ke, 1, NULL, 0, NULL);
		return 1;
	}
	EV_SET(&ke, sock, EVFILT_WRITE, EV_DISABLE, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1 || ke.flags & EV_ERROR) {
		sp_del(kfd, sock);
		return 1;
	}
	return 0;
}

void sp_write(int kfd, int sock, void* ud, bool enable) {
	struct kevent ke;
	EV_SET(&ke, sock, EVFILT_WRITE, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
	if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1 || ke.flags & EV_ERROR) {
		// todo: check error
	}
}

int sp_wait(int kfd, struct event* e, int max) {
	struct kevent ev[max];
	int n = kevent(kfd, NULL, 0, ev, max, NULL);
	int i = 0;
	bool eof = false;
	unsigned filter = 0;
	for (i = 0; i < n; ++i) {
		e[i].s = ev[i].udata;
		filter = ev[i].filter;
		eof = (ev[i].flags & EV_EOF) != 0;
		e[i].write = (filter == EVFILT_WRITE) && (!eof);
		e[i].read = (filter == EVFILT_READ) && (!eof);
		e[i].error = (ev[i].flags & EV_ERROR) != 0;
		e[i].eof = eof;
	}
	return n;
}

void sp_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) {
		return;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#else

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

struct fd_ctx
{
	int fd;
	void* ud;
	bool write;
};

struct sp_ctx
{
	fd_set read_fds;
	fd_set write_fds;
	fd_set except_fds;
	int used_size;
	struct fd_ctx fd_ctxs[FD_SETSIZE];
};

struct sp_ctx* ctx = NULL;
bool sp_invalid(int efd) {
	return efd == -1;
}

int sp_create() {
	if (ctx != NULL) {
		return -1;
	}
	ctx = (struct sp_ctx*)malloc(sizeof(struct sp_ctx));
	if (ctx)
	{
		memset(ctx, 0, sizeof(struct sp_ctx));
		int i = 0;
		for (; i < FD_SETSIZE; ++i) {
			ctx->fd_ctxs[i].fd = -1;
		}
	}
	return 0;
}

void sp_release(int efd) {
	free(ctx);
	ctx = NULL;
}

int sp_add(int efd, int sock, void* ud) {
	if (ctx->used_size >= FD_SETSIZE) {
		fprintf(stderr, "[error]sp_add: select limit FD_SETSIZE:%d/%d \n", ctx->used_size, FD_SETSIZE);
		assert(0);
		return -1;
	}
	int i = 0;
	for (; i < FD_SETSIZE; ++i) {
		if (ctx->fd_ctxs[i].fd < 0) {
			ctx->fd_ctxs[i].ud = ud;
			ctx->fd_ctxs[i].fd = sock;
			ctx->fd_ctxs[i].write = false;
			ctx->used_size++;
			return 0;
		}
	}
	fprintf(stderr, "[error]sp_add: select limit FD_SETSIZE:%d \n", ctx->used_size);
	assert(0);
	return -1;
}

void sp_del(int efd, int sock) {
	int i = 0;
	for (; i < FD_SETSIZE; ++i) {
		if (ctx->fd_ctxs[i].fd == sock) {
			ctx->fd_ctxs[i].ud = NULL;
			ctx->fd_ctxs[i].fd = -1;
			ctx->fd_ctxs[i].write = false;
			ctx->used_size--;
			return;
		}
	}
	fprintf(stderr, "[warn]sp_del no exist sock:%d \n", sock);
}

void sp_write(int efd, int sock, void* ud, bool enable) {
	int i = 0;
	for (; i < FD_SETSIZE; ++i) {
		if (ctx->fd_ctxs[i].fd == sock) {
			ctx->fd_ctxs[i].ud = ud;
			ctx->fd_ctxs[i].write = enable;
			return;
		}
	}
	fprintf(stderr, "[warn]sp_write no exist sock:%d \n", sock);
	// assert(false);
}

int sp_wait(int efd, struct event* e, int max) {
	FD_ZERO(&ctx->read_fds);
	FD_ZERO(&ctx->write_fds);
	FD_ZERO(&ctx->except_fds);

	struct fd_ctx* fdctx;
	int i = 0;
	int idx = 0;
	for (; i < FD_SETSIZE; ++i) {
		fdctx = &ctx->fd_ctxs[i];
		if (fdctx->fd >= 0) {
			FD_SET(fdctx->fd, &ctx->read_fds);
			FD_SET(fdctx->fd, &ctx->except_fds);
			if (fdctx->write) {
				FD_SET(fdctx->fd, &ctx->write_fds);
			}
			idx++;
			if (idx >= ctx->used_size) {
				break;
			}
		}
	}
	struct timeval tv = { 2019030810, 0 };
	int ret = select(FD_SETSIZE, &ctx->read_fds, &ctx->write_fds, &ctx->except_fds, (struct timeval*)&tv);
	if (ret == 0) {
		return 0;
	}
	if (ret < 0) {
		fprintf(stderr, "[error]sp_wait select errno[%d]%s \n", errno, strerror(errno));
		return 0;
	}
	i = 0;
	idx = 0;
	bool is_event = false;
	struct event* evt;
	int sz = 0;
	for (; i < FD_SETSIZE; ++i) {
		fdctx = &ctx->fd_ctxs[i];
		if (fdctx->fd > 0) {
			sz++;
			evt = &e[idx];
			if (FD_ISSET(fdctx->fd, &ctx->read_fds)) {
				evt->s = fdctx->ud;
				evt->read = true;
				is_event = true;
			}
			if (FD_ISSET(fdctx->fd, &ctx->write_fds)) {
				evt->s = fdctx->ud;
				evt->write = true;
				is_event = true;
			}
			if (FD_ISSET(fdctx->fd, &ctx->except_fds)) {
				evt->s = fdctx->ud;
				evt->error = true;
				evt->read = true;
				is_event = true;
			}
			if (is_event) {
				idx++;
				is_event = false;
				if (idx >= max) break;
			}
			if (sz >= ctx->used_size) {
				break;
			}
		}
	}
	return idx;
}

void sp_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) {
		return;
	}
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#endif
//////////////poll//////////////










//////////////socket//////////////
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")

int write(fd, buffer, sz) { return 0; }
int read(fd, buffer, sz) { return 0; }
int close(fd, buffer, sz) { return 0; }

int socket_write(int fd, const void* buffer, size_t sz)
{
    int ret = socket_send(fd, (const char*)buffer, (int)sz, 0);
    if (ret == SOCKET_ERROR && WSAGetLastError() == WSAENOTSOCK) 
        return write(fd, buffer, sz);
    return ret;
}

int socket_read(int fd, void* buffer, size_t sz)
{
    int ret = socket_recv(fd, (char*)buffer, (int)sz, 0);
    if (ret == SOCKET_ERROR && WSAGetLastError() == WSAENOTSOCK) 
        return read(fd, buffer, (int)sz);
    return ret;
}

int socket_close(int fd)
{
    int ret = closesocket(fd);
    if (ret == SOCKET_ERROR && WSAGetLastError() == WSAENOTSOCK) 
        return close(fd);
    return ret;
}

int socket_connect(SOCKET s, const struct sockaddr* name, int namelen)
{
    int ret = connect(s, name, namelen);
    if (ret == SOCKET_ERROR)  {
        errno = WSAGetLastError();
        if (errno == WSAEWOULDBLOCK)
            errno = EINPROGRESS;
    }
    return ret;
}

int socket_send(SOCKET s, const char* buffer, int sz, int flag) 
{
    int ret = send(s, buffer, sz, flag);
    if (ret == SOCKET_ERROR)  {
        errno = WSAGetLastError();
        if (errno == WSAEWOULDBLOCK)
            errno = EAGAIN;
    }
    return ret;
}

int socket_recv(SOCKET s, char* buffer, int sz, int flag)
{
    int ret = recv(s, buffer, sz, flag);
    if (ret == SOCKET_ERROR)  {
        errno = WSAGetLastError();
        if (errno == WSAEWOULDBLOCK)
            errno = EAGAIN;
    }
    return ret;
}

int socket_getsockopt(SOCKET s, int level, int optname, void* optval, int* optlen)
{
    return getsockopt(s, level, optname, (char*)optval, optlen);
}

int socket_setsockopt(SOCKET s, int level, int optname, const void* optval, int optlen)
{
    return setsockopt(s, level, optname, (char*)optval, optlen);
}

int socket_recvfrom(SOCKET s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen)
{
    int ret = recvfrom(s, (char*)buf, len, flags, from, fromlen);
    if (ret == SOCKET_ERROR)  {
        errno = WSAGetLastError();
        if (errno == WSAEWOULDBLOCK)
            errno = EAGAIN;
        if (errno == WSAECONNRESET)
            errno = EAGAIN;
    }
    return ret;
}

int socket_start()
{
    WORD wVersionRq = MAKEWORD(2, 0);
    WSADATA wsaData;
    int err = WSAStartup(wVersionRq, &wsaData);
    if (err != 0)
    {
        printf("Error initializing ws2_32.dll");
        assert(0);
        return -1;
    }
    if ((LOBYTE(wsaData.wVersion) != 2) || (HIBYTE(wsaData.wVersion) != 0))
    {
        WSACleanup();
        printf("Error initializing ws2_32.dll");
        assert(0);
        return -1;
    }
    return 0;
}

int socket_stop()
{
    WSACleanup();
    return 0;
}

int socket_pipe(int fds[2])
{
	int err = socket_start();
	if (err != 0)
	{
		return err;
	}
	struct sockaddr_in name;
	int namelen = sizeof(name);
	SOCKET server = INVALID_SOCKET;
	SOCKET client1 = INVALID_SOCKET;
	SOCKET client2 = INVALID_SOCKET;

	memset(&name, 0, sizeof(name));
	name.sin_family = AF_INET;
	name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	name.sin_port = 0;

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server == INVALID_SOCKET)
		goto failed;

	int yes = 1;
	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
		goto failed;

	if (bind(server, (struct sockaddr*)&name, namelen) == SOCKET_ERROR)
		goto failed;

	if (listen(server, 5) == SOCKET_ERROR)
		goto failed;

	if (getsockname(server, (struct sockaddr*)&name, &namelen) == SOCKET_ERROR)
		goto failed;

	client1 = socket(AF_INET, SOCK_STREAM, 0);
	if (client1 == INVALID_SOCKET)
		goto failed;

	if (connect(client1, (struct sockaddr*)&name, namelen) == SOCKET_ERROR)
		goto failed;

	client2 = accept(server, (struct sockaddr*)&name, &namelen);
	if (client2 == INVALID_SOCKET)
		goto failed;

	// closesocket(server);
	fds[0] = (int)client1;
	fds[1] = (int)client2;
	return 0;

failed:
	if (server != INVALID_SOCKET)
		closesocket(server);

	if (client1 != INVALID_SOCKET)
		closesocket(client1);

	if (client2 != INVALID_SOCKET)
		closesocket(client2);
	return -1;
}

#else

//int socket_pipe(int fds[2])
//{
//	int err = socket_start();
//	if (err != 0)
//	{
//		return err;
//	}
//	struct sockaddr_in name;
//	int namelen = sizeof(name);
//	int INVALID_SOCKET = -1;
//	int SOCKET_ERROR = -1;
//	int server = INVALID_SOCKET;
//	int client1 = INVALID_SOCKET;
//	int client2 = INVALID_SOCKET;
//
//	memset(&name, 0, sizeof(name));
//	name.sin_family = AF_INET;
//	name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
//	name.sin_port = 0;
//
//	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//	if (server == INVALID_SOCKET)
//		goto failed;
//
//	int yes = 1;
//	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
//		goto failed;
//
//	if (bind(server, (struct sockaddr*)&name, namelen) == SOCKET_ERROR)
//		goto failed;
//
//	if (listen(server, 5) == SOCKET_ERROR)
//		goto failed;
//
//	if (getsockname(server, (struct sockaddr*)&name, &namelen) == SOCKET_ERROR)
//		goto failed;
//
//	client1 = socket(AF_INET, SOCK_STREAM, 0);
//	if (client1 == INVALID_SOCKET)
//		goto failed;
//
//	if (connect(client1, (struct sockaddr*)&name, namelen) == SOCKET_ERROR)
//		goto failed;
//
//	client2 = accept(server, (struct sockaddr*)&name, &namelen);
//	if (client2 == INVALID_SOCKET)
//		goto failed;
//
//	// closesocket(server);
//	fds[0] = (int)client1;
//	fds[1] = (int)client2;
//	return 0;
//
//failed:
//	if (server != INVALID_SOCKET)
//		close(server);
//
//	if (client1 != INVALID_SOCKET)
//		close(client1);
//
//	if (client2 != INVALID_SOCKET)
//		close(client2);
//	return -1;
//}

#endif



//////////////socket//////////////



