# OpenSocket
OpenSocket是一个全网最容易实现跨平台的高性能网络并发库。
Linux和安卓用epoll，Win32用IOCP，iOS和Mac用kqueue，其他系统使用select。

结合OpenThread使用，可以轻轻构建在任意平台（包括移动平台）构建高性能并发服务器。

OpenSocket全平台设计，无其他依赖，只有4个源文件，让小白都可以轻松玩转C++高性能网络并发开发。

**OpenLinyou项目设计跨平台服务器框架，在VS或者XCode上写代码，无需任何改动就可以编译运行在Linux上，甚至是安卓和iOS.**
OpenLinyou：https://github.com/openlinyou
OpenThread:https://github.com/openlinyou/openthread
https://gitee.com/linyouhappy/openthread

## 跨平台支持
Linux和安卓使用epoll，Windows使用IOCP(wepoll)，iOS和Mac使用kqueue，其他系统使用select。

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
./helloworld
./httpclient #高并发Http客户端
./httpserver #高并发Http服务器
#浏览器访问:http://127.0.0.1:8888
./server #高并发服务器框架
```

## 全部源文件
+ src/socket_os.h
+ src/socket_os.c
+ src/opensocket.h
+ src/opensocket.cpp
+ src/wepoll.h(only win32)
+ src/wepoll.c(only win32)

## 技术特点
OpenSocket的技术特点：
1. 跨平台设计，提供Linux统一的socket接口。
2. Linux和安卓使用epoll，Windows使用IOCP(wepoll)，iOS和Mac使用kqueue，其他系统使用select。
3. 支持IPv6，小巧迷你，配合OpenThread的多线程三大设计模式，轻轻实现高性能网络。


## 1.Helloworld
使用OpenThread可实现多线程三大模式：Await模式, Factory模式和Actor模式。使用Actor模式设计此demo。
```C++
#include <assert.h>
#include <time.h>
#include <math.h>
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
使用OpenThread的Worker模式设计高并发HttpClient。
```C++
#include <assert.h>
#include <time.h>
#include <math.h>
#include <map>
#include "open/openthread.h"
#include "opensocket.h"
using namespace open;

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
    void setUrl(const std::string& url)
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
        void parseHeader()
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
                    if (j >  0)
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
                    j = line.size() - 1;
                }
            }
            clen_ = std::atoi(headers_["content-length"].c_str());
        }
    };
    HttpResponse response_;
    OpenSync openSync_;
};
struct BaseProto
{
    bool isSocket_;
};
struct SocketProto : public BaseProto
{
    std::shared_ptr<OpenSocketMsg> data_;
};
struct TaskProto : public BaseProto
{
    int fd_;
    OpenSync openSync_;
    std::shared_ptr<HttpRequest> request_;
};
class App
{
    static void SocketFunc(const OpenSocketMsg* msg)
    {
        if (!msg) return;
        if (msg->uid_ >= 0)
        {
            auto proto = std::shared_ptr<SocketProto>(new SocketProto);
            proto->isSocket_ = true;
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

class HttpClient : public OpenThreader
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
        :OpenThreader(name)
    {
        start();
    }
    ~HttpClient()
    {
        for (auto iter = mapFdToTask_.begin(); iter != mapFdToTask_.end(); iter++)
            iter->second.openSync_.wakeup();
    }
    void onHttp(TaskProto& proto)
    {
        auto& request = proto.request_;
        proto.fd_ = App::Instance_.openSocket_.connect(pid(), request->ip_, request->port_);
        request->response_.code_ = -1;
        request->response_.head_.clear();
        request->response_.body_.clear();
        mapFdToTask_[proto.fd_] = proto;
    }
    void onSend(const std::shared_ptr<OpenSocketMsg>& data)
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
    void onRead(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter == mapFdToTask_.end())
        {
            App::Instance_.openSocket_.close(pid(), data->fd_);
            return;
        }
        auto& task = iter->second;
        auto& response = task.request_->response_;
        if (response.code_ == -1)
        {
            response.head_.append(data->data(), data->size());
            const char* ptr = strstr(response.head_.data(), "\r\n\r\n");
            if (!ptr) return;
            response.code_ = 0;
            response.body_.append(ptr + 4);
            response.head_.resize(ptr - response.head_.data() + 2);
            response.parseHeader();
        }
        else
        {
            response.body_.append(data->data(), data->size());
        }
        if (response.clen_ > 0)
        {
            if (response.clen_ >= response.body_.size())
                response.body_.resize(response.clen_);
            App::Instance_.openSocket_.close(pid(), data->fd_);
        }
        else if (response.body_.size() > 2)
        {
            if (response.body_[response.body_.size() - 2] == '\r' && response.body_.back() == '\n')
            {
                response.body_.pop_back();
                response.body_.pop_back();
                App::Instance_.openSocket_.close(pid(), data->fd_);
            }
        }
    }
    void onClose(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter != mapFdToTask_.end())
        {
            iter->second.openSync_.wakeup();
            mapFdToTask_.erase(iter);
        }
    }
    void onSocket(const SocketProto& proto)
    {
        const auto& msg = proto.data_;
        switch (msg->type_)
        {
        case OpenSocket::ESocketData:
            onRead(msg);
            break;
        case OpenSocket::ESocketClose:
            onClose(msg);
            break;
        case OpenSocket::ESocketError:
            printf("[%s]ESocketError:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            onClose(msg);
            break;
        case OpenSocket::ESocketWarning:
            printf("[%s]ESocketWarning:%s\n", ThreadName((int)msg->uid_).c_str(), msg->info());
            break;
        case OpenSocket::ESocketOpen:
            onSend(msg);
            break;
        case OpenSocket::ESocketAccept:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
    virtual void onMsg(OpenThreadMsg& msg)
    {
        const BaseProto* data = msg.data<BaseProto>();
        if (!data) return;
        if (!data->isSocket_)
        {
            TaskProto* proto = msg.edit<TaskProto>();
            if (proto) onHttp(*proto);
        }
        else
        {
            const SocketProto* proto = msg.data<SocketProto>();
            if (proto) onSocket(*proto);
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
        proto->isSocket_ = false;
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
```

## 3.HttpServer
使用OpenThread的Actor模式设计高并发HttpServer。
创建5条线程，1条线程封装成监听者Listener，另外4条线程封装成接收者Accepter。

监听者Listener负责监听socket连接事件，监听到socket新连接事件后，就把fd发给其中一个接收者Accepter；
接收者Accepter接收到socket的fd后，打开该socket连接，与客户端进行网络通信。

此简单的Http接收到Http报文后，进行response一份Http报文，然后关闭socket完成Http短连接操作。

OpenSocket是对poll的封装，一个进程只需要创建一个OpenSocket对象。

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

## 4.设计一个高并发服务器框架
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

## 5.Socket的UDP通信
暂时不提供UDP的demo，如果有人提需求，才考虑提供。
