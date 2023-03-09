#include <assert.h>
#include <map>
#include <set>
#include <memory>

#include "opensocket.h"
#include "open/openthread.h"
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
        openSocket_.send(client.fd_, msg.data(), (int)msg.size());
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
    TestMsg() :count_(0) {}
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
        std::string data = "Hello OpenSocket!";
        openSocket_.send(user.fd_, data.data(), (int)data.size());
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
        {
            idx = hashid_.lookup(proto->fd_);
            if (idx < 0 || idx >= vectUser_.size())
            {
                openSocket_.close(pid_, proto->fd_);
                return;
            }
            std::string buffer = "Hello OpenSocket!";
            openSocket_.send(proto->fd_, buffer.data(), (int)buffer.size());
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
    proto.count_ = 32;
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