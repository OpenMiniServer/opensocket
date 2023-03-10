#ifndef OPEN_SOCKET_HEADER_H
#define OPEN_SOCKET_HEADER_H

#include <stdint.h>
#include <string>
#include <vector>

namespace open
{

class OpenSocket
{
public:
	enum EMsgType
	{
		ESocketData,
		ESocketClose,
		ESocketOpen,
		ESocketAccept,
		ESocketError,
		ESocketUdp,
		ESocketWarning,
	};
	class Msg
	{
	public:
		EMsgType type_;
		int fd_;
		uintptr_t uid_;
		int ud_;
		char* buffer_;
		size_t size_;
		char* option_;

		inline const char* info() const { return buffer_; }
		inline const char* data() const { return buffer_; }
		inline size_t size() const { return size_; }
		Msg();
		~Msg();
	};
	enum EInfoType
	{
		EInfoUnknow,
		EInfoListen,
		EInfoTcp,
		EInfoUdp,
		EInfoBing
	};
	struct Info 
	{
		int id_;
		EInfoType type_;
		uint64_t opaque_;
		uint64_t read_;
		uint64_t write_;
		uint64_t rtime_;
		uint64_t wtime_;
		int64_t wbuffer_;
		std::string name_;
		Info() :id_(0),
		opaque_(0),
		read_(0),
		write_(0),
		rtime_(0),
		wtime_(0),
		wbuffer_(0),
		type_(EInfoUnknow){}
		void clear()
		{
			id_ = 0;
			opaque_ = 0;
			read_   = 0;
			write_  = 0;
			rtime_  = 0;
			wtime_  = 0;
			wbuffer_ = 0;
			type_ = EInfoUnknow;
			name_.clear();
		}
	};
	OpenSocket();
	~OpenSocket();

	bool run(void (*cb)(const Msg*));
	int send(int fd, const void* buffer, int sz);
	int sendLowpriority(int fd, const void* buffer, int sz);
	void nodelay(int fd);

	//tcp part
	int listen(uintptr_t uid, const std::string& host, int port, int backlog);
	int connect(uintptr_t uid, const std::string& host, int port);
	int bind(uintptr_t uid, int fd);
	void close(uintptr_t uid, int fd);
	void shutdown(uintptr_t uid, int fd);
	void start(uintptr_t uid, int fd);

	//udp part
	int udp(uintptr_t uid, const std::string& addr, int port);
	int udpConnect(int fd, const std::string& addr, int port);
	int udpSend(int fd, const char* address, const void* buffer, int sz);
	int udpAddress(const char* address, char* udp_addr, int len);

	void socketInfo(std::vector<Info>& vectInfo);

	static void Sleep(int64_t milliSecond);
	static std::string DomainNameToIp(std::string& domain);
private:
	void forwardMsg(EMsgType type, bool padding, struct socket_message* result);
	int poll();
	static void* ThreadSocket(void* p);

	void (*cb_)(const Msg*);
	bool isRunning_;
	bool isClose_;
	void* socket_server_;
};

typedef OpenSocket::Msg OpenSocketMsg;

};

#endif //OPEN_SOCKET_HEADER_H
