#include <assert.h>
#include <map>
#include <set>
#include <memory>
#include "opensocket.h"
#include "open/openthread.h"
using namespace open;

const std::string TestServerIp_ = "0.0.0.0";
const std::string TestClientIp_ = "127.0.0.1";
const int TestServerPort_ = 8888;

//proto
struct SocketProto : public OpenThreadProto
{
    std::shared_ptr<OpenSocketMsg> data_;
    static inline int ProtoType() { return 1; }
    virtual inline int protoType() const { return SocketProto::ProtoType(); }
};

class ProtoBuffer : public OpenThreadProto
{
    void* data_;
public:
    int msgId_;
    ProtoBuffer() : OpenThreadProto(), msgId_(0) , data_(0) {}
    virtual ~ProtoBuffer() { if (data_) delete data_; }
    template <class T>
    inline T& data()
    {
        T* t = 0;
        if (data_)
        {
            t = dynamic_cast<T*>((T*)data_);
            if (data_ == t) return *t;
            delete data_;
        }
        t = new T;
        data_ = t;
        return *t;
    }
    template <class T>
    inline T& data() const
    {
        if (data_)
        {
            T* t = dynamic_cast<T*>((T*)data_);
            if (data_ == t) return *t;
        }
        assert(false);
        static T t;
        return t;
    }
    static inline int ProtoType() { return 2; }
    virtual inline int protoType() const { return ProtoBuffer::ProtoType(); }
};

////////////App//////////////////////
class App
{
    static void SocketFunc(const OpenSocketMsg* msg)
    {
        if (!msg) return;
        if (msg->uid_ >= 0)
        {
            auto proto = std::shared_ptr<SocketProto>(new SocketProto);
            proto->srcPid_ = -1;
            proto->srcName_ = "OpenSocket";
            proto->data_ = std::shared_ptr<OpenSocketMsg>((OpenSocketMsg*)msg);
            if (!OpenThread::Send((int)msg->uid_, proto))
                printf("SocketFunc dispatch faild pid = %lld\n", msg->uid_);
            return;
        }
        delete msg;
    }
public:
    static App Instance_;
    OpenSocket openSocket_;
    App() { openSocket_.run(App::SocketFunc); }
};
App App::Instance_;


enum EMsgId
{
    new_accept,
    new_client,
    test_client
};


////////////Listener//////////////////////
struct DataNewClient
{
    int accept_fd_;
    std::string addr_;
    DataNewClient() :accept_fd_(-1) {}
};
class Listener : public OpenThreadWorker
{
    std::set<int> setSlaveId_;
    std::vector<int> vectSlaveId_;
    int listen_fd_;
    unsigned int balance_;
    bool isOpening_;
public:
    Listener(const std::string& name)
        :OpenThreadWorker(name),
        listen_fd_(-1)
    {
        isOpening_ = false;
        balance_ = 0;

        registers(SocketProto::ProtoType(), (OpenThreadHandle)&Listener::onSocketProto);
        registers(ProtoBuffer::ProtoType(), (OpenThreadHandle)&Listener::onProtoBuffer);
    }
    virtual ~Listener() {}
    virtual void onStart()
    {
        listen_fd_ = App::Instance_.openSocket_.listen((uintptr_t)pid(), TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listener::onStart faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        App::Instance_.openSocket_.start((uintptr_t)pid(), listen_fd_);
    }
private:
    void onProtoBuffer(const ProtoBuffer& proto)
    {
        if (proto.msgId_ == new_accept)
        {
            auto msg = proto.data<std::string>();
            assert(msg == "listen success!");
            if (proto.srcPid() >= 0)
            {
                if (setSlaveId_.find(proto.srcPid()) == setSlaveId_.end())
                {
                    setSlaveId_.insert(proto.srcPid());
                    vectSlaveId_.push_back(proto.srcPid());
                    printf("Hello OpenSocket, srcPid = %d\n", proto.srcPid());
                }
            }
        }
    }
    void notify(int accept_fd, const std::string& addr)
    {
        if (!vectSlaveId_.empty())
        {
            auto proto = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
            proto->msgId_ = new_client;
            auto& msg = proto->data<DataNewClient>();
            msg.accept_fd_ = accept_fd;
            msg.addr_ = addr;
            if (balance_ >= vectSlaveId_.size())
            {
                balance_ = 0;
            }
            int slaveId = vectSlaveId_[balance_++];
            bool ret = send(slaveId, proto);
            if (ret) return;
        }
        App::Instance_.openSocket_.close(pid_, accept_fd);
    }
    virtual void onSocketProto(const SocketProto& proto)
    {
        const auto msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketAccept:
            notify(msg->ud_, msg->data());
            printf("Listener::onStart [%s]ESocketAccept:acceptFd = %d\n", ThreadName((int)msg->uid_).c_str(), msg->ud_);
            break;
        case OpenSocket::ESocketClose:
            isOpening_ = false;
            break;
        case OpenSocket::ESocketError:
            printf("Listener::onStart [%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Listener::onStart [%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
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
class Accepter : public OpenThreadWorker
{
    int listenId_;
    std::map<int, ServerClient> mapClient_;
public:
    Accepter(const std::string& name)
        :OpenThreadWorker(name),
        listenId_(-1)
    {
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&Accepter::onSocketProto);
        registers(ProtoBuffer::ProtoType(), (OpenThreadHandle)&Accepter::onProtoBuffer);
    }
    virtual ~Accepter() {}

    virtual void onStart() 
    { 
        while (listenId_ < 0)
        {
            listenId_ = ThreadId("listener");
            OpenThread::Sleep(1000);
        }
        auto root = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
        root->msgId_ = new_accept;
        auto& data = root->data<std::string>();
        data = "listen success!";
        send(listenId_, root);
    }
private:
    void onProtoBuffer(const ProtoBuffer& proto)
    {
        if (proto.msgId_ == new_client)
        {
            auto msg = proto.data<DataNewClient>();
            int accept_fd = msg.accept_fd_;
            if (accept_fd >= 0)
            {
                auto iter = mapClient_.find(accept_fd);
                if (iter != mapClient_.end())
                {
                    assert(false);
                    mapClient_.erase(iter);
                    App::Instance_.openSocket_.close(pid(), accept_fd);
                    return;
                }

                auto& client = mapClient_[accept_fd];
                client.fd_ = accept_fd;
                client.addr_ = msg.addr_;
                App::Instance_.openSocket_.start(pid_, accept_fd);
            }
        }
    }

    void onRead(const std::shared_ptr<OpenSocketMsg>& msg)
    {
        auto iter = mapClient_.find(msg->fd_);
        if (iter == mapClient_.end())
        {
            App::Instance_.openSocket_.close(pid_, msg->fd_);
            return;
        }
        auto& client = iter->second;
        client.buffer_.append(msg->data(), msg->size());
        auto& buffer = client.buffer_;
        if (buffer.empty())
            return;

        std::string data = "[" + name_  + "]" + client.addr_ + ":" + buffer;
        client.buffer_.clear();
        App::Instance_.openSocket_.send(client.fd_, data.data(), (int)data.size());
    }

    virtual void onSocketProto(const SocketProto& proto)
    {
        const auto msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketData:
            onRead(msg);
            break;
        case OpenSocket::ESocketClose:
            mapClient_.erase(msg->fd_);
            break;
        case OpenSocket::ESocketError:
            mapClient_.erase(msg->fd_);
            printf("Accepter::onStart [%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Accepter::onStart [%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketOpen:
        {
            auto iter = mapClient_.find(msg->fd_);
            if (iter == mapClient_.end())
            {
                App::Instance_.openSocket_.close(pid_, msg->fd_);
                return;
            }
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
    TestMsg() :count_(0) {}
};
struct User
{
    int fd_;
    int userId_;
    std::string buffer_;
};
class Client : public OpenThreadWorker
{
    std::map<int, User> mapUser_;
public:
    Client(const std::string& name)
        :OpenThreadWorker(name)
    {
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&Client::onSocketProto);
        registers(ProtoBuffer::ProtoType(), (OpenThreadHandle)&Client::onProtoBuffer);
    }
    virtual ~Client() {}
    virtual void onStart()
    {
    }
private:
    void onProtoBuffer(const ProtoBuffer& proto)
    {
        if (proto.msgId_ == test_client)
        {
            auto msg = proto.data<TestMsg>();
            int count = msg.count_;
            int fd = 0;
            for (int i = 0; i < count; i++)
            {
                fd = App::Instance_.openSocket_.connect(pid_, TestClientIp_, TestServerPort_);
                if (fd < 0)
                {
                    printf("Client::start_test faild fd = %d\n", fd);
                    assert(0);
                }
                auto& user = mapUser_[fd];
                user.fd_ = fd;
                user.userId_ = pid_ + i * 1000;
                printf("Client::start_test[%s] fd = %d \n", name().c_str(), fd);
            }
        }
    }
    void onRead(const std::shared_ptr<OpenSocketMsg>& msg)
    {
        auto iter = mapUser_.find(msg->fd_);
        if (iter == mapUser_.end())
        {
            App::Instance_.openSocket_.close(pid_, msg->fd_);
            return;
        }
        auto& user = iter->second;
        user.buffer_.append(msg->data(), msg->size());
        printf("Client::onRead[%s:%d]:%s\n", name().c_str(), user.userId_, user.buffer_.c_str());
        user.buffer_.clear();
        OpenSocket::Sleep(500);
        std::string data = "Hello OpenSocket!";
        App::Instance_.openSocket_.send(user.fd_, data.data(), (int)data.size());
    }
    virtual void onSocketProto(const SocketProto& proto)
    {
        const auto msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketData:
            onRead(msg);
            break;
        case OpenSocket::ESocketClose:
            mapUser_.erase(msg->fd_);
            break;
        case OpenSocket::ESocketError:
            mapUser_.erase(msg->fd_);
            printf("Client::onStart [%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Client::onStart [%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketOpen:
        {
            auto iter = mapUser_.find(msg->fd_);
            if (iter == mapUser_.end())
            {
                App::Instance_.openSocket_.close(pid_, msg->fd_);
                return;
            }
            std::string buffer = "Hello OpenSocket!";
            App::Instance_.openSocket_.send(msg->fd_, buffer.data(), (int)buffer.size());
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
    std::vector<OpenThreadWorker*> vectWorker;

    printf("start server==>>\n");
    //server
    std::vector<OpenThreadWorker*> vectServer =
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
    std::vector<OpenThreadWorker*> vectClient =
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

    auto proto = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
    proto->msgId_ = test_client;
    auto& data = proto->data<TestMsg>();
    data.count_ = 10;
    //
    for (size_t i = 0; i < vectClient.size(); i++)
    {
        vectClient[i]->send(vectClient[i]->pid(), proto);
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