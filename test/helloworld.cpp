#include <assert.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "opensocket.h"
#include "open/openthread.h"
using namespace open;

const std::string TestServerIp_ = "0.0.0.0";
const std::string TestClientIp_ = "127.0.0.1";
const int TestServerPort_ = 8888;
OpenSocket openSocket_;

struct ProtoBuffer
{
    bool isSocket_;
    int acceptFd_;
    std::string addr_;
    std::shared_ptr<OpenSocketMsg> data_;
    ProtoBuffer() :
        isSocket_(0), acceptFd_(0) {}
};

static void SocketFunc(const OpenSocketMsg* msg)
{
    if (!msg) return;
    if (msg->uid_ < 0)
    {
        delete msg; return;
    }
    auto proto = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
    proto->isSocket_ = true;
    proto->data_     = std::shared_ptr<OpenSocketMsg>((OpenSocketMsg*)msg);
    bool ret = OpenThread::Send((int)msg->uid_, proto);
    if (!ret)
    {
        printf("SocketFunc dispatch faild pid = %d\n", (int)msg->uid_);
    }
}

// Listen
static int listen_fd_ = 0;
void ListenThread(OpenThreadMsg& msg)
{
    int pid = msg.pid();
    auto& pname = msg.name();
    assert(pname == "listen");
    if (msg.state_ == OpenThread::START)
    {
        while (OpenThread::ThreadId("accept") < 0) OpenThread::Sleep(100);
        listen_fd_ = openSocket_.listen((uintptr_t)pid, TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listen::START faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        openSocket_.start((uintptr_t)pid, listen_fd_);
    }
    else if (msg.state_ == OpenThread::RUN)
    {
        const ProtoBuffer* proto = msg.data<ProtoBuffer>();
        if (!proto || !proto->isSocket_ || !proto->data_) return;
        auto& socketMsg = proto->data_;
        switch (socketMsg->type_)
        {
        case OpenSocket::ESocketAccept:
        {
            printf("Listen::RUN [%s]ESocketAccept: new client. acceptFd:%d, client:%s\n", pname.c_str(), socketMsg->ud_, socketMsg->info());
            auto proto = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
            proto->addr_     = socketMsg->info();
            proto->isSocket_ = false;
            proto->acceptFd_ = socketMsg->ud_;
            bool ret = OpenThread::Send("accept", proto);
            assert(ret);
        }
            break;
        case OpenSocket::ESocketClose:
            printf("Listen::RUN [%s]ESocketClose:linten close, listenFd:%d\n", pname.c_str(), socketMsg->fd_);
            break;
        case OpenSocket::ESocketError:
            printf("Listen::RUN [%s]ESocketError:%s\n", pname.c_str(), socketMsg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Listen::RUN [%s]ESocketWarning:%s\n", pname.c_str(), socketMsg->info());
            break;
        case OpenSocket::ESocketOpen:
            printf("Listen::RUN [%s]ESocketOpen:linten open, listenFd:%d\n", pname.c_str(), socketMsg->fd_);
            break;
        case OpenSocket::ESocketUdp:
        case OpenSocket::ESocketData:
            assert(false);
            break;
        default:
            break;
        }
    }
    else if (msg.state_ == OpenThread::STOP)
    {
        openSocket_.close(pid, listen_fd_);
    }
}

// Accept
void AcceptThread(OpenThreadMsg& msg)
{
    int pid     = msg.pid();
    auto& pname = msg.name();
    assert(pname == "accept");
    if (msg.state_ == OpenThread::START)
    {
    }
    else if (msg.state_ == OpenThread::RUN)
    {
        const ProtoBuffer* proto = msg.data<ProtoBuffer>();
        if (!proto) return;
        if (!proto->isSocket_)
        {
            printf("Accept::RUN  [%s]open accept client:%s\n", pname.c_str(), proto->addr_.c_str());
            openSocket_.start(pid, proto->acceptFd_);
        }
        else
        {
            if (!proto->data_) return;
            auto& socketMsg = proto->data_;
            switch (socketMsg->type_)
            {
            case OpenSocket::ESocketData:
            {
                //recevie from client
                {
                    auto size = socketMsg->size();
                    auto data = socketMsg->data();
                    assert(size >= 4);
                    int len = *(int*)data;
                    std::string buffer;
                    buffer.append(data + 4, len);
                    assert(buffer == "Waiting for you!");
                }
                
                //response to client
                {
                    char buffer[256] = { 0 };
                    std::string tmp = "Of Course,I Still Love You!";
                    *(int*)buffer = (int)tmp.size();
                    memcpy(buffer + 4, tmp.data(), tmp.size());
                    openSocket_.send(socketMsg->fd_, buffer, (int)(4 + tmp.size()));
                }
            }
                break;
            case OpenSocket::ESocketOpen:
                printf("Accept::RUN [%s]ESocketClose:accept client open, acceptFd:%d\n", pname.c_str(), socketMsg->fd_);
                break;
            case OpenSocket::ESocketClose:
                printf("Accept::RUN [%s]ESocketClose:accept client close, acceptFd:%d\n", pname.c_str(), socketMsg->fd_);
                break;
            case OpenSocket::ESocketError:
                printf("Accept::RUN  [%s]ESocketError:accept client  %s\n", pname.c_str(), socketMsg->info());
                break;
            case OpenSocket::ESocketWarning:
                printf("Accept::RUN  [%s]ESocketWarning:%s\n", pname.c_str(), socketMsg->info());
                break;
            case OpenSocket::ESocketAccept:
            case OpenSocket::ESocketUdp:
                assert(false);
                break;
            default:
                break;
            }
        }
    }
}
//client
static int client_fd_ = 0;
void ClientThread(OpenThreadMsg& msg)
{
    int pid = msg.pid();
    auto& pname = msg.name();
    assert(pname == "client");
    if (msg.state_ == OpenThread::START)
    {
        while (OpenThread::ThreadId("accept") < 0) OpenThread::Sleep(100);
        client_fd_ = openSocket_.connect(pid, TestClientIp_, TestServerPort_);
    }
    else if (msg.state_ == OpenThread::RUN)
    {
        const ProtoBuffer* proto = msg.data<ProtoBuffer>();
        if (!proto || !proto->isSocket_ || !proto->data_) return;
        auto& socketMsg = proto->data_;
        switch (socketMsg->type_)
        {
        case OpenSocket::ESocketData:
        {
            //recevie from client
            auto size = socketMsg->size();
            auto data = socketMsg->data();
            assert(size >= 4);
            int len = *(int*)data;
            std::string buffer;
            buffer.append(data + 4, len);
            assert(buffer == "Of Course,I Still Love You!");
            openSocket_.close(pid, socketMsg->fd_);

            OpenThread::StopAll();
        }
        break;
        case OpenSocket::ESocketOpen:
        {
            assert(client_fd_ == socketMsg->fd_);
            printf("Client::RUN [%s]ESocketClose:Client client open, clientFd:%d\n", pname.c_str(), socketMsg->fd_);
            char buffer[256] = {0};
            std::string tmp = "Waiting for you!";
            *(int*)buffer = (int)tmp.size();
            memcpy(buffer + 4, tmp.data(), tmp.size());
            openSocket_.send(client_fd_, buffer, (int)(4 + tmp.size()));
        }
            break;
        case OpenSocket::ESocketClose:
            printf("Client::RUN [%s]ESocketClose:Client client close, clientFd:%d\n", pname.c_str(), socketMsg->fd_);
            break;
        case OpenSocket::ESocketError:
            printf("Client::RUN  [%s]ESocketError:Client client  %s\n", pname.c_str(), socketMsg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Client::RUN  [%s]ESocketWarning:Client %s\n", pname.c_str(), socketMsg->info());
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
        
    }
}
int main()
{
    // create and start thread
    OpenThread::Create("listen", ListenThread);
    OpenThread::Create("accept", AcceptThread);
    OpenThread::Create("client", ClientThread);

    // run OpenSocket
    openSocket_.run(SocketFunc);

    OpenThread::ThreadJoinAll();
    printf("Pause\n");
    return getchar();
}
