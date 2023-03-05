# OpenSocket
OpenSocket是一个超简单易用的跨平台高性能网络并发库。

结合OpenThread使用，可以轻轻构建在任意平台（包括移动平台）构建高性能并发服务器。

OpenSocket全平台设计，无其他依赖，只有4个源文件，让小白都可以轻松玩转C++高性能网络并发开发。

***OpenLinyou项目设计跨平台服务器框架，在VS或者XCode上写代码，无需任何改动就可以编译运行在Linux上，甚至是安卓和iOS.***
OpenLinyou：https://github.com/openlinyou

## 跨平台支持
Linux和安卓使用epoll，iOS和Mac使用kqueue，其他系统（Windows）使用select，故io数量不能超过64个。

## 编译和执行
请安装cmake工具，用cmake可以构建出VS或者XCode工程，就可以在vs或者xcode上编译运行。
源代码：https://github.com/openlinyou/opensocket
```
#克隆项目
git clone https://github.com/openlinyou/opensocket
cd ./opensocket
#创建build工程目录
mkdir build
cd build
cmake ..
#如果是win32，在该目录出现opensocket.sln，点击它就可以启动vs写代码调试
make
./tcp
./http
#浏览器访问:http://127.0.0.1:8888
```

## 全部源文件
. src/socket_os.h
. src/socket_os.c
. src/opensocket.h
. src/opensocket.cpp

## 技术特点
OpenSocket的技术特点：
1. 跨平台设计，提供Linux统一的socket接口，支持安卓和iOS。
2. Linux和安卓使用epoll，iOS和Mac使用kqueue，其他系统（Windows）使用select。
3. 支持IPv6，小巧迷你，配合OpenThread使用，轻轻构建Actor Model框架。


## 1.简单的Http
创建5条线程，1条线程封装成监听者Listener，另外4条线程封装成接收者Accepter。

监听者Listener负责监听socket连接，监听到socket后，就把fd发给其中一个接收者Accepter；
接收者Accepter接收到socket的fd后，启动打开socket，与客户端连接socket连接。

此简单的Http接收到Http报文后，进行response一份Http报文，然后关闭socket完成Http短连接操作。

OpenSocket是对poll的封装，一个进程只需要创建一个OpenSocket对象。
由于Windows使用select方案，socket数量不能超过64个。

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
//OpenSocket对象，一个进程只需要创建一个OpenSocket对象。
static OpenSocket openSocket_;
//OpenSocket的消息回调函数，内部线程执行，必须立刻把消息派发给其他线程处理。
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
//监听socket连接事件，把socket连接事件发给接收者
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
        //启动listen
        listen_fd_ = openSocket_.listen((uintptr_t)pid(), TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listener::onStart faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        //start的作用是开启poll监听，监听listen_fd_的消息。
        openSocket_.start((uintptr_t)pid(), listen_fd_);
    }
    //接收接收者的注册消息，监听到的socket事件会发给接收者处理
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
    //把监听到的fd和ip发给其中一个接收者，此时新socket还没有打开连接
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
            //监听socket连接
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
//一个客户端对象
struct Client
{
    int fd_; // socket的fd
    std::string addr_; //ip:port
    std::string buffer_; //接收到的网络数据
    Client() :fd_(-1) {}
};
//接收监听者的fd，打开socket连接，负责与客户端通信
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
        //指定接收者处理socket的个数。即可以与8个客户端通信。
        maxClient_ = 8;
        hashid_.init(maxClient_);
        vectClient_.resize(maxClient_);
        mapKeyFunc_["new_accept"] = { (Handle)&Accepter::new_accept };
    }
    virtual ~Accepter() {}
    virtual void onStart() 
    { 
        //先等待listener启动完成
        while (listenId_ < 0)
        {
            listenId_ = ThreadId("listener");
            OpenThread::Sleep(1000);
        }
        //向listener发送注册消息
        send<std::string>(listenId_, "regist_slave", "listen success!");
    }
    //接收Listener的新socket消息，同时打开socket连接，接收客户端消息。
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
            //打开socket连接，接收客户端消息。把accept_fd加入到poll。
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
        //向客户端发送Http报文
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
            //接收客户端消息
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
            //与客户端关闭连接消息
        case OpenSocket::ESocketClose:
            idx = hashid_.remove(proto->fd_);
            if (idx >= 0 && idx < vectClient_.size())
            {
                vectClient_[idx].fd_ = -1;
                vectClient_[idx].buffer_.clear();
            }
            break;
            //与客户端通信发生错误消息
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
            //打开客户端通信的消息
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
    //启动OpenSocket，只能启动一次。
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

## 2.Socket的TCP通信
Listener负责监听socket连接事件，发socket连接事件发给Accepter。

Accepter负责接收Listener发过来的socket连接事件，并与socket进行通信。

Client是客户端集群，使用它可以对服务器进行压力测试。

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

## 3.Socket的UDP通信
暂时不提供UDP的demo，如果有人提需求，才考虑提供。
