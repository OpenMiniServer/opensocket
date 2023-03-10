# OpenSocket
OpenSocket is a super simple and easy-to-use cross-platform high-performance network concurrency library.

**Linux and Android use epoll, iOS and Mac use kqueue, Windows use IOCP(wepoll).other systems use select.**

Combined with OpenThread, you can easily build high-performance concurrent servers on any platform (including mobile platforms).

OpenSocket is designed for all platforms with no other dependencies and only 4 source files, making it easy for beginners to play with C++ high-performance network concurrency development.

**The OpenLinyou project designs a cross-platform server framework. Write code in VS or XCode and run it on Linux without any changes, even on Android and iOS.**
OpenLinyou：https://github.com/openlinyou
OpenThread:https://github.com/openlinyou/openthread
https://gitee.com/linyouhappy/openthread

## Cross-platform support
Linux and Android use epoll, iOS and Mac use kqueue, Windows use IOCP(wepoll).other systems use select.

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
./helloworld
./httpclient
./httpserver
# Visit with browser: http://127.0.0.1:8888
./server
```

## All source files
+ src/socket_os.h
+ src/socket_os.c
+ src/opensocket.h
+ src/opensocket.cpp
+ src/wepoll.h(only win32)
+ src/wepoll.c(only win32)

## Technical features
The technical features of OpenSocket:
1. Cross-platform design, providing a unified socket interface for Linux, supporting Android and iOS.
2. Linux and Android use epoll, iOS and Mac use kqueue, Windows use IOCP(wepoll).other systems use select.
3. Supports IPv6, small and miniaturized; used with OpenThread to easily build an Actor Model framework.


## 1.Helloworld
Using OpenThread can achieve three major modes of multithreading: Await mode, Factory mode and Actor mode. Design this demo using Actor mode.
```C++
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
        printf("SocketFunc dispatch faild pid = %lld\n", msg->uid_);
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

```

## 2.HttpClient
Designing a high-concurrency HttpClient using OpenThread’s Worker mode.
```C++
#include <assert.h>
#include <time.h>
#include <math.h>
#include <map>
#include <string.h>
#include "open/openthread.h"
#include "opensocket.h"
using namespace open;

////////////HttpRequest//////////////////////
class HttpRequest
{
    std::string url_;
public:
    std::map<std::string, std::string> headers_;
    int port_;
    std::string host_;
    std::string ip_;
    std::string path_;
    std::string method_;
    std::string body_;
    HttpRequest() :port_(80) {}
    std::string& operator[](const std::string& key) { return headers_[key]; }
    void setUrl(const std::string& url);
    inline void operator=(const std::string& url) { setUrl(url); }

    struct HttpResponse
    {
        int code_;
        int clen_;
        std::string head_;
        std::string body_;
        //std::multimap<std::string, std::string> headers_;
        std::map<std::string, std::string> headers_;
        std::string& operator[](const std::string& key) { return headers_[key]; }

        HttpResponse():code_(0), clen_(0) {}
        void parseHeader();
        bool pushData(const char* data, size_t size);
    };
    HttpResponse response_;
    OpenSync openSync_;
};

////////////Proto//////////////////////
struct SocketProto : public OpenThreadProto
{
    std::shared_ptr<OpenSocketMsg> data_;
    static inline int ProtoType() { return 1; }
    virtual inline int protoType() const { return SocketProto::ProtoType(); }
};

struct TaskProto : public OpenThreadProto
{
    int fd_;
    OpenSync openSync_;
    std::shared_ptr<HttpRequest> request_;
    static inline int ProtoType() { return 2; }
    virtual inline int protoType() const { return TaskProto::ProtoType(); }
    TaskProto() :fd_(0) {}
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
            proto->srcPid_  = -1;
            proto->srcName_ = "OpenSocket";
            proto->data_ = std::shared_ptr<OpenSocketMsg>((OpenSocketMsg*)msg);
            if (!OpenThread::Send((int)msg->uid_, proto))
                printf("SocketFunc dispatch faild pid = %lld\n", msg->uid_);
        }
        else delete msg;
    }
public:
    static App Instance_;
    OpenSocket openSocket_;
    App() {  openSocket_.run(App::SocketFunc); }
};
App App::Instance_;


////////////HttpClient//////////////////////
class HttpClient : public OpenThreadWorker
{
    //Factory
    class Factory
    {
        const std::vector<HttpClient*> vectWorker_;
    public:
        Factory()
            :vectWorker_({
                new HttpClient("HttpClient1"),
                new HttpClient("HttpClient2"),
                new HttpClient("HttpClient3"),
                new HttpClient("HttpClient4"),
                }) {}
        HttpClient* getWorker()
        {
            if (vectWorker_.empty()) return 0;
            return vectWorker_[std::rand() % vectWorker_.size()];
        }
    };
    static Factory Instance_;

    // HttpClient
    HttpClient(const std::string& name)
        :OpenThreadWorker(name)
    {
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&HttpClient::onSocketProto);
        registers(TaskProto::ProtoType(), (OpenThreadHandle)&HttpClient::onTaskProto);
        start();
    }
    ~HttpClient()
    {
        for (auto iter = mapFdToTask_.begin(); iter != mapFdToTask_.end(); iter++)
            iter->second.openSync_.wakeup();
    }

private:
    void onTaskProto(TaskProto& proto)
    {
        auto& request = proto.request_;
        proto.fd_ = App::Instance_.openSocket_.connect(pid(), request->ip_, request->port_);
        request->response_.code_ = -1;
        request->response_.head_.clear();
        request->response_.body_.clear();
        mapFdToTask_[proto.fd_] = proto;
    }
    void onSendHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter == mapFdToTask_.end())
        {
            App::Instance_.openSocket_.close(pid(), data->fd_);
            return;
        }
        auto& task = iter->second;
        auto& request = task.request_;
        std::string buffer = request->method_ + " " + request->path_ + " HTTP/1.1 \r\n";
        auto iter1 = request->headers_.begin();
        for (; iter1 != request->headers_.end(); iter1++)
        {
            buffer.append(iter1->first + ": " + iter1->second + "\r\n");
        }
        if (!request->body_.empty())
        {
            buffer.append("Content-Length:" + std::to_string(request->body_.size()) + "\r\n\r\n");
            buffer.append(request->body_);
            buffer.append("\r\n");
        }
        else
        {
            buffer.append("\r\n");
        }
        App::Instance_.openSocket_.send(task.fd_, buffer.data(), (int)buffer.size());
    }
    void onReadHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter == mapFdToTask_.end())
        {
            App::Instance_.openSocket_.close(pid(), data->fd_);
            return;
        }
        auto& task = iter->second;
        auto& response = task.request_->response_;
        if (response.pushData(data->data(), data->size()))
        {
            App::Instance_.openSocket_.close(pid(), data->fd_);
        }
    }
    void onCloseHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter != mapFdToTask_.end())
        {
            iter->second.openSync_.wakeup();
            mapFdToTask_.erase(iter);
        }
    }
    void onSocketProto(const SocketProto& proto)
    {
        const auto& msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketData:
            onReadHttp(msg);
            break;
        case OpenSocket::ESocketClose:
            onCloseHttp(msg);
            break;
        case OpenSocket::ESocketError:
            printf("[%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            onCloseHttp(msg);
            break;
        case OpenSocket::ESocketWarning:
            printf("[%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketOpen:
            onSendHttp(msg);
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
    std::map<int, TaskProto> mapFdToTask_;
public:
    static bool Http(std::shared_ptr<HttpRequest>& request)
    {
        if (request->ip_.empty())
        {
            assert(false);
            return false;
        }
        request->response_.code_ = -1;
        auto worker = Instance_.getWorker();
        if (!worker)  return false;
        auto proto = std::shared_ptr<TaskProto>(new TaskProto);
        proto->request_ = request;
        bool ret = OpenThread::Send(worker->pid(), proto);
        assert(ret);
        proto->openSync_.await();
        return ret;
    }
};
HttpClient::Factory HttpClient::Instance_;

int main()
{
    auto request = std::shared_ptr<HttpRequest>(new HttpRequest);
    //Stock Market Latest Dragon and Tiger List
    request->setUrl("http://reportdocs.static.szse.cn/files/text/jy/jy230308.txt");
    request->method_ = "GET";

    (*request)["Host"] = "reportdocs.static.szse.cn";
    (*request)["Accept"] = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7";
    (*request)["Accept-Encoding"] = "gzip,deflate";
    (*request)["Accept-Language"] = "zh-CN,zh;q=0.9";
    (*request)["Cache-Control"] = "max-age=0";
    (*request)["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36(KHTML, like Gecko) Chrome/110.0.0.0 Safari/537.36";
    (*request)["Upgrade-Insecure-Requests"] = "1";

    HttpClient::Http(request);
    auto& response = request->response_;
    printf("code:%d, header:%s\n", response.code_, response.head_.c_str());
    return getchar();
}


void HttpRequest::setUrl(const std::string& url)
{
    if (url.empty()) return;
    url_ = url;
    int len = (int)url.length();
    char* ptr = (char*)url.c_str();
    if (len >= 8)
    {
        if (memcmp(ptr, "http://", strlen("http://")) == 0)
            ptr += strlen("http://");
        else if (memcmp(ptr, "https://", strlen("https://")) == 0)
            ptr += strlen("https://");
    }
    const char* tmp = strstr(ptr, "/");
    path_.clear();
    if (tmp != 0)
    {
        path_.append(tmp);
        host_.clear();
        host_.append(ptr, tmp - ptr);
    }
    else
    {
        host_ = ptr;
    }
    port_ = 80;
    ip_.clear();
    ptr = (char*)host_.c_str();
    tmp = strstr(ptr, ":");
    if (tmp != 0)
    {
        ip_.append(ptr, tmp - ptr);
        tmp += 1;
        port_ = atoi(tmp);
    }
    else
    {
        ip_ = ptr;
    }
    ip_ = OpenSocket::DomainNameToIp(ip_);
}


void HttpRequest::HttpResponse::parseHeader()
{
    if (!headers_.empty() || head_.size() < 12) return;
    std::string line;
    const char* ptr = strstr(head_.c_str(), "\r\n");
    if (!ptr) return;
    code_ = 0;
    clen_ = 0;
    line.append(head_.c_str(), ptr - head_.c_str());
    for (size_t i = 0; i < line.size(); i++)
    {
        if (line[i] == ' ')
        {
            while (i < line.size() && line[i] == ' ') ++i;
            code_ = std::atoi(line.data() + i);
            break;
        }
    }
    if (code_ <= 0) return;
    line.clear();
    int k = -1;
    int j = -1;
    std::string key;
    std::string value;
    for (size_t i = ptr - head_.c_str() + 2; i < head_.size() - 1; i++)
    {
        if (head_[i] == '\r' && head_[i + 1] == '\n')
        {
            if (j > 0)
            {
                k = 0;
                while (k < line.size() && line[k] == ' ') ++k;
                while (k >= 0 && line.back() == ' ') line.pop_back();
                value = line.data() + j + 1;
                while (j >= 0 && line[j] == ' ') j--;
                key.clear();
                key.append(line.data(), j);
                for (size_t x = 0; x < key.size(); x++)
                    key[x] = std::tolower(key[x]);
                headers_[key] = value;
            }
            ++i;
            j = -1;
            line.clear();
            continue;
        }
        line.push_back(head_[i]);
        if (j < 0 && line.back() == ':')
        {
            j = (int)line.size() - 1;
        }
    }
    clen_ = std::atoi(headers_["content-length"].c_str());
}

bool HttpRequest::HttpResponse::pushData(const char* data, size_t size)
{
    if (code_ == -1)
    {
        head_.append(data, size);
        const char* ptr = strstr(head_.data(), "\r\n\r\n");
        if (!ptr) return false;
        code_ = 0;
        body_.append(ptr + 4);
        head_.resize(ptr - head_.data() + 2);
        parseHeader();
    }
    else
    {
        body_.append(data, size);
    }
    if (clen_ > 0)
    {
        if (clen_ >= body_.size())
        {
            body_.resize(clen_);
            return true;
        }
    }
    else if (body_.size() > 2)
    {
        if (body_[body_.size() - 2] == '\r' && body_.back() == '\n')
        {
            body_.pop_back();
            body_.pop_back();
            return true;
        }
    }
    return false;
}
```

## 3.HttpServer
Create 5 threads: 1 thread is encapsulated as a Listener and the other 4 threads are encapsulated as Accepters.

The Listener is responsible for listening to socket connections. After listening to the socket, it sends the fd to one of the Accepters; After receiving the socket’s fd from the Listener, the Accepter opens the socket and connects with the client’s socket connection.

After receiving an Http message from this simple Http server, it responds with an Http message and then closes the socket to complete an Http short connection operation.

OpenSocket is a wrapper for poll; only one OpenSocket object needs to be created per process. Since Windows uses select scheme, number of sockets cannot exceed 64.

```C++
#include <assert.h>
#include <map>
#include <set>
#include <memory>
#include <string.h>
#include "opensocket.h"
#include "open/openthread.h"
using namespace open;

const std::string TestServerIp_ = "0.0.0.0";
const int TestServerPort_ = 8888;

//msgType == 1
struct SocketProto : public OpenThreadProto
{
    std::shared_ptr<OpenSocketMsg> data_;
    static inline int ProtoType() { return 1; }
    virtual inline int protoType() const { return SocketProto::ProtoType(); }
};

//msgType == 2
struct RegisterProto : public OpenThreadProto
{
    int srcPid_;
    static inline int ProtoType() { return 2; }
    virtual inline int protoType() const { return RegisterProto::ProtoType(); }
    RegisterProto() :srcPid_(-1) {}
};

//msgType == 3
struct NewClientProto : public OpenThreadProto
{
    int accept_fd_;
    std::string addr_;
    static inline int ProtoType() { return 3; }
    virtual inline int protoType() const { return NewClientProto::ProtoType(); }
    NewClientProto() : accept_fd_(-1) {}
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
        }
        else
        {
            delete msg;
        }
    }
public:
    static App Instance_;
    OpenSocket openSocket_;
    App() { openSocket_.run(App::SocketFunc); }
};
App App::Instance_;

////////////Listener//////////////////////
class Listener : public OpenThreadWorker
{
    int listen_fd_;
    unsigned int balance_;
    std::set<int> setSlaveId_;
    std::vector<int> vectSlaveId_;
public:
    Listener(const std::string& name)
        :OpenThreadWorker(name),
        listen_fd_(-1)
    {
        balance_ = 0;
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&Listener::onSocketProto);
        registers(RegisterProto::ProtoType(), (OpenThreadHandle)&Listener::onRegisterProto);
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
        printf("HTTP: %s:%d\n", TestServerIp_.c_str(), TestServerPort_);
    }
    void onRegisterProto(const RegisterProto& proto)
    {
        if (proto.srcPid_ >= 0)
        {
            if (setSlaveId_.find(proto.srcPid_) == setSlaveId_.end())
            {
                setSlaveId_.insert(proto.srcPid_);
                vectSlaveId_.push_back(proto.srcPid_);
                printf("Hello OpenSocket HttpServer, srcPid = %d\n", proto.srcPid_);
            }
        }
    }
    // new client socket dispatch to Accept
    void notifyToSlave(int accept_fd, const std::string& addr)
    {
        if (!vectSlaveId_.empty())
        {
            auto proto = std::shared_ptr<NewClientProto>(new NewClientProto);
            proto->accept_fd_ = accept_fd;
            proto->addr_ = addr;
            if (balance_ >= vectSlaveId_.size())
            {
                balance_ = 0;
            }
            int slaveId = vectSlaveId_[balance_++];
            if (OpenThread::Send(slaveId, proto))
            {
                return;
            }
            printf("Listener::notifyToSlave send faild pid = %d\n", slaveId);
        }
        App::Instance_.openSocket_.close(pid_, accept_fd);
    }
    void onSocketProto(const SocketProto& proto)
    {
        const auto& msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketAccept:
            // linsten new client socket
            notifyToSlave(msg->ud_, msg->data());
            printf("Listener::onSocket [%s]ESocketAccept:acceptFd = %d\n", ThreadName((int)msg->uid_).c_str(), msg->ud_);
            break;
        case OpenSocket::ESocketClose:
            break;
        case OpenSocket::ESocketError:
            printf("Listener::onSocket [%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketWarning:
            printf("Listener::onSocket [%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketOpen:
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

////////////HttpRequest//////////////////////
struct HttpRequest
{
    int fd_;
    std::string addr_;

    std::string method_;
    std::string url_;

    int code_;
    int clen_;
    std::string head_;
    std::string body_;
    std::map<std::string, std::string> headers_;
    HttpRequest() :fd_(-1), code_(-1), clen_(-1) {}

    //GET /xx/xx HTTP/x.x
    bool parseHeader();
    bool pushData(const char* data, size_t size);
};

////////////Accepter//////////////////////
class Accepter : public OpenThreadWorker
{
    int listenId_;
    std::map<int, HttpRequest> mapClient_;
public:
    Accepter(const std::string& name)
        :OpenThreadWorker(name),
        listenId_(-1)
    {
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&Accepter::onSocketProto);
        registers(NewClientProto::ProtoType(), (OpenThreadHandle)&Accepter::onNewClientProto);
    }
    virtual ~Accepter() {}
    virtual void onStart() 
    { 
        while (listenId_ < 0)
        {
            listenId_ = ThreadId("listener");
            OpenThread::Sleep(1000);
        }
        auto proto = std::shared_ptr<RegisterProto>(new RegisterProto);
        proto->srcPid_ = pid();
        if (OpenThread::Send(listenId_, proto))
            return;
        printf("Accepter::onStart send faild pid = %d\n", listenId_);
    }
    void onNewClientProto(const NewClientProto& proto)
    {
        int accept_fd = proto.accept_fd_;
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
            client.addr_ = proto.addr_;
            App::Instance_.openSocket_.start(pid_, accept_fd);
        }
    }
    //GET /xx/xx HTTP/x.x
    void onReadHttp(const std::shared_ptr<OpenSocketMsg> msg)
    {
        auto iter = mapClient_.find(msg->fd_);
        if (iter == mapClient_.end())
        {
            App::Instance_.openSocket_.close(pid_, msg->fd_);
            return;
        }
        auto& request = iter->second;
        if (!request.pushData(msg->data(), msg->size()))
        {
            //Header too large.close connet.
            if (request.head_.size() > 1024)
                App::Instance_.openSocket_.close(pid_, msg->fd_);
            return;
        }
        printf("new client:url = %s\n", request.url_.c_str());
        std::string content;
        content.append("<div>It's work!</div><br/>" + request.addr_ + "request:" + request.url_);
        std::string buffer = "HTTP/1.1 200 OK\r\ncontent-length:" + std::to_string(content.size()) + "\r\n\r\n" + content;
        App::Instance_.openSocket_.send(msg->fd_, buffer.data(), (int)buffer.size());
    }
    virtual void onSocketProto(const SocketProto& proto)
    {
        const auto& msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketData:
            onReadHttp(msg);
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
int main()
{
    printf("start server==>>\n");
    std::vector<OpenThreader*> vectServer = {
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


bool HttpRequest::parseHeader()
{
    if (!headers_.empty() || head_.size() < 12) return true;
    std::string line;
    const char* ptr = strstr(head_.c_str(), "\r\n");
    if (!ptr) return false;
    clen_ = -1;
    line.append(head_.c_str(), ptr - head_.c_str());

    int state = 0;
    method_.clear();
    url_.clear();
    for (size_t k = 0; k < line.size(); ++k)
    {
        if (state == 0)
        {
            if (line[k] != ' ')
            {
                method_.push_back(line[k]);
                continue;
            }
            state = 1;
            while (k < line.size() && line[k] == ' ') ++k;
            if (line[k] != ' ') --k;
        }
        else
        {
            if (line[k] != ' ')
            {
                url_.push_back(line[k]);
                continue;
            }
            break;
        }
    }

    line.clear();
    int k = -1;
    int j = -1;
    std::string key;
    std::string value;
    for (size_t i = ptr - head_.c_str() + 2; i < head_.size() - 1; i++)
    {
        if (head_[i] == '\r' && head_[i + 1] == '\n')
        {
            if (j > 0)
            {
                k = 0;
                while (k < line.size() && line[k] == ' ') ++k;
                while (k >= 0 && line.back() == ' ') line.pop_back();
                value = line.data() + j + 1;
                while (j >= 0 && line[j] == ' ') j--;
                key.clear();
                key.append(line.data(), j);
                for (size_t x = 0; x < key.size(); x++)
                    key[x] = std::tolower(key[x]);
                headers_[key] = value;
            }
            ++i;
            j = -1;
            line.clear();
            continue;
        }
        line.push_back(head_[i]);
        if (j < 0 && line.back() == ':')
        {
            j = (int)line.size() - 1;
        }
    }
    clen_ = std::atoi(headers_["content-length"].c_str());
    return true;
}

bool HttpRequest::pushData(const char* data, size_t size)
{
    if (code_ == -1)
    {
        head_.append(data, size);
        const char* ptr = strstr(head_.data(), "\r\n\r\n");
        if (!ptr) return false;
        code_ = 0;
        body_.append(ptr + 4);
        head_.resize(ptr - head_.data() + 2);
        if (!parseHeader()) return false;
    }
    else
    {
        body_.append(data, size);
    }
    if (clen_ >= 0)
    {
        if (clen_ == 0 && clen_ == body_.size())
        {
            return true;
        }
        if (clen_ >= body_.size())
        {
            body_.resize(clen_);
            return true;
        }
    }
    else if (body_.size() > 2)
    {
        if (body_[body_.size() - 2] == '\r' && body_.back() == '\n')
        {
            body_.pop_back();
            body_.pop_back();
            return true;
        }
    }
    return false;
}
```

## 4.Socket TCP communication
he Listener is responsible for listening to socket connection events and sending socket connection events to the Accepter.

The Accepter is responsible for receiving socket connection events sent by the Listener and communicating with the socket.

Client is a client cluster that can be used to perform stress tests on the server.

```C++
#include <assert.h>
#include <map>
#include <set>
#include <memory>
#include <string.h>
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
    std::shared_ptr<void> data_;
public:
    int msgId_;
    ProtoBuffer()
        : OpenThreadProto()
        , msgId_(0)
        , data_(0) {}
    virtual ~ProtoBuffer() {}
    template <class T>
    inline T& data()
    {
        T* t = 0;
        if (data_)
        {
            t = dynamic_cast<T*>((T*)data_.get());
            if (data_.get() == t) return *t;
            data_.reset();
        }
        t = new T;
        data_ = std::shared_ptr<T>(t);
        return *t;
    }
    template <class T>
    inline T& data() const
    {
        if (data_)
        {
            T* t = dynamic_cast<T*>((T*)data_.get());
            if (data_.get() == t) return *t;
        }
        assert(false);
        static T t;
        return t;
    }
    static inline int ProtoType() { return (int)(uintptr_t) & (ProtoType); }
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
```

## 3.Socket UDP communication 
A UDP demo is not currently provided; it will only be considered if someone requests it.
