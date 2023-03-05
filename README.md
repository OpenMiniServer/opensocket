# OpenSocket
OpenSocket is a super simple and easy-to-use cross-platform high-performance network concurrency library.

Combined with OpenThread, you can easily build high-performance concurrent servers on any platform (including mobile platforms).

OpenSocket is designed for all platforms with no other dependencies and only 4 source files, making it easy for beginners to play with C++ high-performance network concurrency development.

**The OpenLinyou project designs a cross-platform server framework. Write code in VS or XCode and run it on Linux without any changes, even on Android and iOS.**
OpenLinyou：https://github.com/openlinyou

## Cross-platform support
Linux and Android use epoll, iOS and Mac use kqueue, other systems (Windows) use select, so the number of io cannot exceed 64.

## Compilation and execution
Please install the cmake tool. With cmake you can build a VS or XCode project and compile and run it on VS or XCode. 
Source code:https://github.com/openlinyou/opensocket
```
# Clone the project
git clone https://github.com/openlinyou/opensocket
cd ./opensocket
# Create a build project directory
mkdir build
cd build
cmake ..
# If it's win32, opensocket.sln will appear in this directory. Click it to start VS for coding and debugging.
make
./tcp
./http
# Visit with browser: http://127.0.0.1:8888
```

## All source files
+ src/socket_os.h
+ src/socket_os.c
+ src/opensocket.h
+ src/opensocket.cpp

## Technical features
The technical features of OpenSocket:
1. Cross-platform design, providing a unified socket interface for Linux, supporting Android and iOS.
2. Linux and Android use epoll, iOS and Mac use kqueue, other systems (Windows) use select.
3. Supports IPv6, small and miniaturized; used with OpenThread to easily build an Actor Model framework.


## 1.Simple Http
Create 5 threads: 1 thread is encapsulated as a Listener and the other 4 threads are encapsulated as Accepters.

The Listener is responsible for listening to socket connections. After listening to the socket, it sends the fd to one of the Accepters; After receiving the socket’s fd from the Listener, the Accepter opens the socket and connects with the client’s socket connection.

After receiving an Http message from this simple Http server, it responds with an Http message and then closes the socket to complete an Http short connection operation.

OpenSocket is a wrapper for poll; only one OpenSocket object needs to be created per process. Since Windows uses select scheme, number of sockets cannot exceed 64.

```C++
#include <assert.h>
#include <map>
#include <set>
#include <memory>
#include "opensocket.h"
#include "openthread.h"
#include "worker.h"
using namespace open;

const std::string TestServerIp_ = "0.0.0.0";
const int TestServerPort_ = 8888;
//OpenSocket object; only one OpenSocket object needs to be created per process. 
static OpenSocket openSocket_;
// The message callback function of OpenSocket is executed by an internal thread 
// and must immediately dispatch the message to other threads for processing.
static void SocketFunc(const OpenSocketMsg* msg)
{
    if (msg->uid_ >= 0)
    {
        auto data = std::shared_ptr<Data>(new Data());
        auto proto = std::shared_ptr<const OpenSocketMsg>(msg);
        data->setProto(EProtoSocket, proto);
        bool ret = OpenThread::Send((int)msg->uid_, data);
        assert(ret);
    }
    else
    {
        delete msg;
    }
}
////////////Listener//////////////////////
struct ProtoBuffer
{
    int accept_fd_;
    std::string addr_;
};
//Listen to socket connection events and send socket connection events to the accepter.
class Listener : public Worker
{
    std::set<int> setSlaveId_;
    std::vector<int> vectSlaveId_;
    int listen_fd_;
    unsigned int balance_;
    bool isOpening_;
public:
    Listener(const std::string& name)
        :Worker(name),
        listen_fd_(-1)
    {
        isOpening_ = false;
        balance_ = 0;
        mapKeyFunc_["regist_slave"] = { (Handle)&Listener::regist_slave };
    }
    virtual ~Listener() {}
    virtual void onStart()
    {
        // Start socket listen. 
        listen_fd_ = openSocket_.listen((uintptr_t)pid(), TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listener::onStart faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        // The function of start is to start poll listening and listen to messages from listen_fd_.
        openSocket_.start((uintptr_t)pid(), listen_fd_);
    }
    // Receive registration messages from accepters; 
    // sockets that are listened to will be sent to accepters for processing.
    void regist_slave(const Data& data)
    {
        auto proto = data.proto<std::string>();
        if (!proto)
        {
            assert(false);
            return;
        }
        assert(*proto == "listen success!");
        if (data.srcPid() >= 0)
        {
            if (setSlaveId_.find(data.srcPid()) == setSlaveId_.end())
            {
                setSlaveId_.insert(data.srcPid());
                vectSlaveId_.push_back(data.srcPid());
                printf("Hello OpenThread, srcPid = %d\n", data.srcPid());
            }
        }
    }
    // Send the listened fd and ip to one of the accepters; 
    // at this time the new socket has not yet opened a connection.
    void notify(int accept_fd, const std::string& addr)
    {
        if (!vectSlaveId_.empty())
        {
            ProtoBuffer proto;
            proto.accept_fd_ = accept_fd;
            proto.addr_ = addr;
            if (balance_ >= vectSlaveId_.size())
            {
                balance_ = 0;
            }
            int slaveId = vectSlaveId_[balance_++];
            bool ret = send<ProtoBuffer>(slaveId, "new_accept", proto);
            if (ret)
            {
                return;
            }
        }
        openSocket_.close(pid_, accept_fd);
    }
    virtual void onSocket(const Data& data)
    {
        auto proto = data.proto<OpenSocketMsg>();
        if (!proto)
        {
            assert(false);
            return;
        }
        switch (proto->type_)
        {
            //Listen for socket connections
        case OpenSocket::ESocketAccept:
            notify(proto->ud_, proto->data());
            printf("Listener::onStart [%s]ESocketAccept:acceptFd = %d\n", ThreadName((int)proto->uid_).c_str(), proto->ud_);
            break;
        case OpenSocket::ESocketClose:
            isOpening_ = false;
            break;
        case OpenSocket::ESocketError:
            printf("Listener::onStart [%s]ESocketError:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Listener::onStart [%s]ESocketWarning:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketOpen:
            isOpening_ = true;
            break;
        case OpenSocket::ESocketUdp:
        case OpenSocket::ESocketData:
            assert(false);
            break;
        default:
            break;
        }
    }
};

////////////Accepter//////////////////////
//client object
struct Client
{
    int fd_; // socket’s fd 
    std::string addr_; //ip:port
    std::string buffer_; //received network data
    Client() :fd_(-1) {}
};

//Receive the listener’s new socket fd, open the socket connection and communicate with the client.
class Accepter : public Worker
{
    int listenId_;
    int maxClient_;
    Hashid hashid_;
    std::vector<Client> vectClient_;
public:
    Accepter(const std::string& name)
        :Worker(name),
        listenId_(-1)
    {
        //Specify the number of sockets that the accepter can handle.
        maxClient_ = 8;
        hashid_.init(maxClient_);
        vectClient_.resize(maxClient_);
        mapKeyFunc_["new_accept"] = { (Handle)&Accepter::new_accept };
    }
    virtual ~Accepter() {}
    virtual void onStart() 
    { 
        //Wait for listener to start first.
        while (listenId_ < 0)
        {
            listenId_ = ThreadId("listener");
            OpenThread::Sleep(1000);
        }
        //Send registration message to listener.
        send<std::string>(listenId_, "regist_slave", "listen success!");
    }
    //Receive new socket messages from Listener and open socket connection to receive messages from clients.
    void new_accept(const Data& data)
    {
        auto proto = data.proto<ProtoBuffer>();
        if (!proto)
        {
            assert(false);
            return;
        }
        int accept_fd = proto->accept_fd_;
        if (accept_fd >= 0)
        {
            if (hashid_.full())
            {
                openSocket_.close(pid_, accept_fd);
                return;
            }
            int idx = hashid_.insert(accept_fd);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, accept_fd);
                return;
            }
            vectClient_[idx].fd_ = accept_fd;
            vectClient_[idx].addr_ = proto->addr_;
            vectClient_[idx].buffer_.clear();
            //Open socket connection and receive messages from clients. Add accept_fd to poll.
            openSocket_.start(pid_, accept_fd);
        }
    }
    //GET /xx/xx HTTP/x.x
    void onReadHttp(Client& client)
    {
        auto& buffer = client.buffer_;
        if (buffer.size() < 8)
            return;
        if (buffer[0] != 'G' || buffer[1] != 'E' || buffer[2] != 'T')
            return;
        auto idx = buffer.find(" HTTP/");
        if (idx == std::string::npos)
        {
            if (buffer.size() > 1024)
            {
                openSocket_.close(pid_, client.fd_);
            }
            return;
        }
        std::string url;
        size_t i = 3;
        while (buffer[i] == ' ' && i < buffer.size()) ++i;
        for (; i < buffer.size(); ++i)
        {
            if (buffer[i] == ' ') break;
            url.push_back(buffer[i]);
        }
        printf("new client:url = %s\n", url.c_str());
        std::string content;
        content.append("<div>It's work!</div><br/>" + client.addr_ + "request:" + url);
        std::string msg = "HTTP/1.1 200 OK\r\ncontent-length:" + std::to_string(content.size()) + "\r\n\r\n" + content;
        //Send Http message to client.
        openSocket_.send(client.fd_, msg);
    }
    virtual void onSocket(const Data& data)
    {
        auto proto = data.proto<OpenSocketMsg>();
        if (!proto)
        {
            assert(false);
            return;
        }
        int idx = 0;
        switch (proto->type_)
        {
            //Receive message from client. 
        case OpenSocket::ESocketData:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            vectClient_[idx].buffer_.append(proto->data(), proto->size());
            onReadHttp(vectClient_[idx]);
            break;
            //Close connection with client message.
        case OpenSocket::ESocketClose:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectClient_.size())
            {
                vectClient_[idx].fd_ = -1;
                vectClient_[idx].buffer_.clear();
            }
            break;
            //Error message when communicating with client. 
        case OpenSocket::ESocketError:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectClient_.size())
            {
                vectClient_[idx].fd_ = -1;
                vectClient_[idx].buffer_.clear();
            }
            printf("Accepter::onStart [%s]ESocketError:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Accepter::onStart [%s]ESocketWarning:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
            //Open communication with client message. 
        case OpenSocket::ESocketOpen:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
};
int main()
{
    //Start OpenSocket; can only be started once
    openSocket_.run(SocketFunc);
    printf("start server==>>\n");
    std::vector<Worker*> vectServer = {
        new Listener("listener"),
        new Accepter("accepter1"),
        new Accepter("accepter2"),
        new Accepter("accepter3"),
        new Accepter("accepter4")
    };
    for (size_t i = 0; i < vectServer.size(); ++i)
        vectServer[i]->start();
    printf("wait close==>>\n");
    OpenThread::ThreadJoinAll();
    for (size_t i = 0; i < vectServer.size(); ++i)
        delete vectServer[i];
    vectServer.clear();
    printf("Pause\n");
    return getchar();
}
```

## 2.Socket TCP communication
he Listener is responsible for listening to socket connection events and sending socket connection events to the Accepter.

The Accepter is responsible for receiving socket connection events sent by the Listener and communicating with the socket.

Client is a client cluster that can be used to perform stress tests on the server.

```C++
#include <assert.h>
#include <map>
#include <set>
#include <memory>

#include "opensocket.h"
#include "openthread.h"
#include "worker.h"
using namespace open;

const std::string TestServerIp_ = "0.0.0.0";
const std::string TestClientIp_ = "127.0.0.1";
const int TestServerPort_ = 8888;

static OpenSocket openSocket_;
static void SocketFunc(const OpenSocketMsg* msg)
{
    if (msg->uid_ >= 0)
    {
        auto data = std::shared_ptr<Data>(new Data());
        auto proto = std::shared_ptr<const OpenSocketMsg>(msg);
        data->setProto(EProtoSocket, proto);
        bool ret = OpenThread::Send((int)msg->uid_, data);
        assert(ret);
    }
    else
    {
        delete msg;
    }
}
////////////Listener//////////////////////
struct ProtoBuffer
{
    int accept_fd_;
    std::string addr_;
};
class Listener : public Worker
{
    std::set<int> setSlaveId_;
    std::vector<int> vectSlaveId_;
    int listen_fd_;
    unsigned int balance_;
    bool isOpening_;
public:
    Listener(const std::string& name)
        :Worker(name),
        listen_fd_(-1)
    {
        isOpening_ = false;
        balance_ = 0;
        mapKeyFunc_["regist_slave"] = { (Handle)&Listener::regist_slave };
    }
    virtual ~Listener() {}
    virtual void onStart()
    {
        listen_fd_ = openSocket_.listen((uintptr_t)pid(), TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listener::onStart faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        openSocket_.start((uintptr_t)pid(), listen_fd_);
    }
    void regist_slave(const Data& data)
    {
        auto proto = data.proto<std::string>();
        if (!proto)
        {
            assert(false);
            return;
        }
        assert(*proto == "listen success!");
        if (data.srcPid() >= 0)
        {
            if (setSlaveId_.find(data.srcPid()) == setSlaveId_.end())
            {
                setSlaveId_.insert(data.srcPid());
                vectSlaveId_.push_back(data.srcPid());
                printf("Hello OpenThread, srcPid = %d\n", data.srcPid());
            }
        }
    }
    void notify(int accept_fd, const std::string& addr)
    {
        if (!vectSlaveId_.empty())
        {
            ProtoBuffer proto;
            proto.accept_fd_ = accept_fd;
            proto.addr_ = addr;
            if (balance_ >= vectSlaveId_.size())
            {
                balance_ = 0;
            }
            int slaveId = vectSlaveId_[balance_++];
            bool ret = send<ProtoBuffer>(slaveId, "new_accept", proto);
            if (ret)
            {
                return;
            }
        }
        openSocket_.close(pid_, accept_fd);
    }
    virtual void onSocket(const Data& data)
    {
        auto proto = data.proto<OpenSocketMsg>();
        if (!proto)
        {
            assert(false);
            return;
        }
        switch (proto->type_)
        {
        case OpenSocket::ESocketAccept:
            notify(proto->ud_, proto->data());
            printf("Listener::onStart [%s]ESocketAccept:acceptFd = %d\n", ThreadName((int)proto->uid_).c_str(), proto->ud_);
            break;
        case OpenSocket::ESocketClose:
            isOpening_ = false;
            break;
        case OpenSocket::ESocketError:
            printf("Listener::onStart [%s]ESocketError:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Listener::onStart [%s]ESocketWarning:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketOpen:
            isOpening_ = true;
            break;
        case OpenSocket::ESocketUdp:
        case OpenSocket::ESocketData:
            assert(false);
            break;
        default:
            break;
        }
    }
};

////////////Accepter//////////////////////
struct ServerClient
{
    int fd_;
    std::string addr_;
    std::string buffer_;
    ServerClient() :fd_(-1) {}
};
class Accepter : public Worker
{
    int listenId_;
    int maxClient_;
    Hashid hashid_;
    std::vector<ServerClient> vectClient_;
public:
    Accepter(const std::string& name)
        :Worker(name),
        listenId_(-1)
    {
        maxClient_ = 8;
        hashid_.init(maxClient_);
        vectClient_.resize(maxClient_);
        mapKeyFunc_["new_accept"] = { (Handle)&Accepter::new_accept };
    }
    virtual ~Accepter() {}

    virtual void onStart() 
    { 
        while (listenId_ < 0)
        {
            listenId_ = ThreadId("listener");
            OpenThread::Sleep(1000);
        }
        send<std::string>(listenId_, "regist_slave", "listen success!");
    }

    void new_accept(const Data& data)
    {
        auto proto = data.proto<ProtoBuffer>();
        if (!proto)
        {
            assert(false);
            return;
        }
        int accept_fd = proto->accept_fd_;
        if (accept_fd >= 0)
        {
            if (hashid_.full())
            {
                openSocket_.close(pid_, accept_fd);
                return;
            }
            int idx = hashid_.insert(accept_fd);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, accept_fd);
                return;
            }
            vectClient_[idx].fd_ = accept_fd;
            vectClient_[idx].addr_ = proto->addr_;
            vectClient_[idx].buffer_.clear();
            openSocket_.start(pid_, accept_fd);
        }
    }
    void onRead(ServerClient& client)
    {
        auto& buffer = client.buffer_;
        if (buffer.empty())
            return;
        std::string msg = "[" + name_  + "]" + client.addr_ + ":" + buffer;
        client.buffer_.clear();
        openSocket_.send(client.fd_, msg);
    }
    virtual void onSocket(const Data& data)
    {
        auto proto = data.proto<OpenSocketMsg>();
        if (!proto)
        {
            assert(false);
            return;
        }
        int idx = 0;
        switch (proto->type_)
        {
        case OpenSocket::ESocketData:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            vectClient_[idx].buffer_.append(proto->data(), proto->size());
            onRead(vectClient_[idx]);
            break;
        case OpenSocket::ESocketClose:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectClient_.size())
            {
                vectClient_[idx].fd_ = -1;
                vectClient_[idx].buffer_.clear();
            }
            break;
        case OpenSocket::ESocketError:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectClient_.size())
            {
                vectClient_[idx].fd_ = -1;
                vectClient_[idx].buffer_.clear();
            }
            printf("Accepter::onStart [%s]ESocketError:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Accepter::onStart [%s]ESocketWarning:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketOpen:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectClient_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
};
struct TestMsg
{
    int count_;
};
struct User
{
    int fd_;
    int userId_;
    std::string buffer_;
};
class Client : public Worker
{
    int maxUser_;
    Hashid hashid_;
    std::vector<User> vectUser_;
public:
    Client(const std::string& name)
        :Worker(name)
    {
        maxUser_ = 0;
        mapKeyFunc_["start_test"] = { (Handle)&Client::start_test };
    }
    virtual ~Client() {}
    virtual void onStart()
    {
    }
    void start_test(const Data& data)
    {
        if (!vectUser_.empty())
        {
            assert(false);
            return;
        }
        auto proto = data.proto<TestMsg>();
        if (!proto || proto->count_ <= 0)
        {
            assert(false);
            return;
        }
        int fd = -1;
        int idx = 0;
        maxUser_ = proto->count_;
        hashid_.init(maxUser_);
        vectUser_.resize(maxUser_);
        for (int i = 0; i < maxUser_; i++)
        {
            fd = openSocket_.connect(pid_, TestClientIp_, TestServerPort_);
            if (fd < 0)
            {
                printf("Client::start_test faild fd = %d\n", fd);
                assert(0);
            }
            idx = hashid_.insert(fd);
            if (idx < 0 || idx >= vectUser_.size())
            {
                openSocket_.close(pid_, fd);
                return;
            }
            vectUser_[idx].fd_ = fd;
            vectUser_[idx].userId_ = pid_ + i * 1000;
            vectUser_[idx].buffer_.clear();
            printf("Client::start_test[%s] fd = %d \n", name().c_str(), fd);
        }
    }
    void onRead(User& user)
    {
        auto& buffer = user.buffer_;
        printf("Client::onRead[%s:%d]:%s\n", name().c_str(), user.userId_, user.buffer_.c_str());
        user.buffer_.clear();
        OpenSocket::Sleep(500);
        openSocket_.send(user.fd_, "Hello OpenSocket!");
    }
    virtual void onSocket(const Data& data)
    {
        auto proto = data.proto<OpenSocketMsg>();
        if (!proto)
        {
            assert(false);
        }
        int idx = 0;
        switch (proto->type_)
        {
        case OpenSocket::ESocketData:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectUser_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            vectUser_[idx].buffer_.append(proto->data(), proto->size());
            onRead(vectUser_[idx]);
            break;
        case OpenSocket::ESocketClose:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectUser_.size())
            {
                vectUser_[idx].fd_ = -1;
                vectUser_[idx].buffer_.clear();
            }
            break;
        case OpenSocket::ESocketError:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectUser_.size())
            {
                vectUser_[idx].fd_ = -1;
                vectUser_[idx].buffer_.clear();
            }
            printf("Client::onStart [%s]ESocketError:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Client::onStart [%s]ESocketWarning:%s\n", ThreadName((int)proto->uid_).c_str(), proto->info());
            break;
        case OpenSocket::ESocketOpen:
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectUser_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            openSocket_.send(proto->fd_, "Hello OpenSocket!");
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
};
int main()
{
    openSocket_.run(SocketFunc);
    std::vector<Worker*> vectWorker;

    printf("start server==>>\n");
    //server
    std::vector<Worker*> vectServer =
    {
        new Listener("listener"),
        new Accepter("accepter1"),
        new Accepter("accepter2"),
        new Accepter("accepter3"),
        new Accepter("accepter4")
    };
    for (size_t i = 0; i < vectServer.size(); i++)
    {
        vectWorker.push_back(vectServer[i]);
        vectServer[i]->start();
    }

    printf("slepp 3000 ms==>>\n");
    //wait server start.
    OpenThread::Sleep(3000);

    printf("start client==>>\n");
    //client
    std::vector<Worker*> vectClient =
    {
        new Client("client1"),
        new Client("client2"),
        new Client("client3"),
        new Client("client4"),
    };
    for (size_t i = 0; i < vectClient.size(); i++)
    {
        vectWorker.push_back(vectClient[i]);
        vectClient[i]->start();
    }

    TestMsg proto;
    proto.count_ = 4;
    for (size_t i = 0; i < vectClient.size(); i++)
    {
        vectClient[i]->send<TestMsg>(vectClient[i]->pid(), "start_test", proto);
    }

    printf("wait close==>>\n");
    //wait all worker
    OpenThread::ThreadJoinAll();
    for (size_t i = 0; i < vectWorker.size(); i++)
    {
        delete vectWorker[i];
    }
    vectWorker.clear();
    printf("Pause\n");
    return getchar();
}
```

## 3.Socket UDP communication 
A UDP demo is not currently provided; it will only be considered if someone requests it.
