# OpenSocket
OpenSocket是一个全网最容易实现跨平台的高性能网络并发库。

**Linux和安卓用epoll，Win32用IOCP，iOS和Mac用kqueue，其他系统使用select。**

结合OpenThread使用，可以轻轻在任意平台（包括移动平台）构建高性能并发服务器。

OpenThread可实现三大多线程设计模式。

**OpenLinyou项目设计跨平台服务器框架，在VS或者XCode上写代码，无需任何改动就可以编译运行在Linux上，甚至是安卓和iOS.**
OpenLinyou：https://github.com/openlinyou
OpenThread:https://github.com/openlinyou/openthread
https://gitee.com/linyouhappy/openthread

## 跨平台支持
Linux和安卓使用epoll，Windows使用IOCP(wepoll)，iOS和Mac使用kqueue，其他系统使用select。

## 编译和执行
请安装cmake工具，用cmake可以构建出VS或者XCode工程，就可以在vs或者xcode上编译运行。
源代码：https://github.com/openlinyou/opensocket
https://gitee.com/linyouhappy/opensocket
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

## 1.HelloWorld
使用OpenThread创建3条线程：listen，accept和client。

1. 线程listen专门负责监听，把监听到新客户端连接，发送给accept线程。
2. 线程accept可以有多个，收到listen线程发送过来的新socket事件，打开socket连接，与客户端通信。
3. 线程client表示客户端，client的socket向 listen请求连接，listen把请求连接发给其中一个accept线程，accept线程接到连接后，与客户端通信。

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

//全局的OpenSocket对象。
OpenSocket openSocket_;

//线程之间交换数据结构
struct ProtoBuffer
{
    bool isSocket_;
    int acceptFd_;
    std::string addr_;
    std::shared_ptr<OpenSocketMsg> data_;
    ProtoBuffer() :
        isSocket_(0), acceptFd_(0) {}
};

//OpenSocket线程，不允许有业务处理，消息需要立刻派发到其他线程
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
    //把proto发到 msg->uid_指定的线程。
    bool ret = OpenThread::Send((int)msg->uid_, proto);
    if (!ret)
    {
        printf("SocketFunc dispatch faild pid = %lld\n", msg->uid_);
    }
}

// 监听网络连接的线程
static int listen_fd_ = 0;
void ListenThread(OpenThreadMsg& msg)
{
    //线程id
    int pid = msg.pid();
    //线程名称
    auto& pname = msg.name();
    assert(pname == "listen");
    //线程启动消息
    if (msg.state_ == OpenThread::START)
    {
        //等待accept线程启动完，才继续执行
        while (OpenThread::ThreadId("accept") < 0) OpenThread::Sleep(100);
        //监听IP和端口，内部会执行bind操作。
        listen_fd_ = openSocket_.listen((uintptr_t)pid, TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listen::START faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        //把listen_fd加入到poll监听事件，并绑定线程pid，该socket任何事件都会发到该线程（listen线程）。
        openSocket_.start((uintptr_t)pid, listen_fd_);
    }
    //其他线程发过来的消息
    else if (msg.state_ == OpenThread::RUN)
    {
        const ProtoBuffer* proto = msg.data<ProtoBuffer>();
        if (!proto || !proto->isSocket_ || !proto->data_) return;
        //openSocket_.start 开启poll监听socket事件，并指定线程id。故此处会收到新客户端socket连接的消息
        auto& socketMsg = proto->data_;
        switch (socketMsg->type_)
        {
        //监听客户端连接事件，把事件发给accept线程。
        case OpenSocket::ESocketAccept:
        {
            printf("Listen::RUN [%s]ESocketAccept: new client. acceptFd:%d, client:%s\n", pname.c_str(), socketMsg->ud_, socketMsg->info());
            auto proto = std::shared_ptr<ProtoBuffer>(new ProtoBuffer);
            proto->addr_     = socketMsg->info();
            proto->isSocket_ = false;
            proto->acceptFd_ = socketMsg->ud_;
            //把客户端的fd和ip发给accept线程。
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
            //因为listen线程只有listen，故不会收到socket数据。
        case OpenSocket::ESocketData:
        case OpenSocket::ESocketUdp:
            assert(false);
            break;
        default:
            break;
        }
    }
    //线程退出消息
    else if (msg.state_ == OpenThread::STOP)
    {
        //线程退出的时候，关闭listen监听socket
        openSocket_.close(pid, listen_fd_);
    }
}

// Accept线程，接收到新客户端连接，开启socket通信。
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
        //会收到两种消息，一种是listen线程发过来的消息，另一种是OpenSocket发过来的消息
        if (!proto->isSocket_)
        {
            //listen线程发过来的消息
            printf("Accept::RUN  [%s]open accept client:%s\n", pname.c_str(), proto->addr_.c_str());
            //拿到客户端的fd，加入到poll监听，并绑定pid指定的线程ID（即Accept线程）。
            //poll监听到任何事件都发到这个线程。
            openSocket_.start(pid, proto->acceptFd_);
        }
        else
        {
            //OpenSocket发过来的消息
            if (!proto->data_) return;
            auto& socketMsg = proto->data_;
            switch (socketMsg->type_)
            {
            //客户端发过来的socket数据流
            case OpenSocket::ESocketData:
            {
                //recevie from client
                {
                    //本次接收到socket的数据大小
                    auto size = socketMsg->size();
                    //本次接收到socket的数据
                    auto data = socketMsg->data();
                    assert(size >= 4);
                    int len = *(int*)data;
                    std::string buffer;
                    buffer.append(data + 4, len);
                    assert(buffer == "Waiting for you!");
                }
                //接到客户端socket消息后，立刻给客户端发消息。
                //response to client
                {
                    char buffer[256] = { 0 };
                    std::string tmp = "Of Course,I Still Love You!";
                    *(int*)buffer = (int)tmp.size();
                    memcpy(buffer + 4, tmp.data(), tmp.size());
                    //给客户端发消息，需要指定该客户端的fd
                    openSocket_.send(socketMsg->fd_, buffer, (int)(4 + tmp.size()));
                }
            }
                break;
            case OpenSocket::ESocketOpen:
                //拿到客户端的fd，加入到poll监听，成功就会收到该消息。
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
                //因为accept线程没有绑定listen的socket，故不会收到该消息。
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

//客户端线程，模拟一个客户端。可以拆开，用另一个进程实现，模拟客户端访问服务器。
static int client_fd_ = 0;
void ClientThread(OpenThreadMsg& msg)
{
    //客户端线程id
    int pid = msg.pid();
    //客户端线程名称
    auto& pname = msg.name();
    assert(pname == "client");
    if (msg.state_ == OpenThread::START)
    {
        //因为在同一个进程，所以，需要等listen等线程启动完毕，才能连接。
        while (OpenThread::ThreadId("accept") < 0) OpenThread::Sleep(100);
        //连接服务器ip和端口，并把fd与客户端线程绑定，fd的任何信息都会发到客户端线程。
        client_fd_ = openSocket_.connect(pid, TestClientIp_, TestServerPort_);
    }
    else if (msg.state_ == OpenThread::RUN)
    {
        //作为客户端线程，只有OpenSocket消息。
        const ProtoBuffer* proto = msg.data<ProtoBuffer>();
        if (!proto || !proto->isSocket_ || !proto->data_) return;
        auto& socketMsg = proto->data_;
        switch (socketMsg->type_)
        {
        case OpenSocket::ESocketData:
        {
            //接收到服务器的socket数据
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
            //如果与服务器连接成果，就会收到此消息
            assert(client_fd_ == socketMsg->fd_);
            printf("Client::RUN [%s]ESocketClose:Client client open, clientFd:%d\n", pname.c_str(), socketMsg->fd_);
            char buffer[256] = {0};
            std::string tmp = "Waiting for you!";
            *(int*)buffer = (int)tmp.size();
            memcpy(buffer + 4, tmp.data(), tmp.size());
            //与服务器连接成功，就向服务器发消息。发消息只需要指定fd。
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
    // 创建三个线程。listen线程、accept线程和client线程。
    OpenThread::Create("listen", ListenThread);
    OpenThread::Create("accept", AcceptThread);
    OpenThread::Create("client", ClientThread);
    // 启动OpenSocket。一个进程只需要一个OpenSocket。
    openSocket_.run(SocketFunc);
    //等待全部子线程结束，否则一直阻塞。
    OpenThread::ThreadJoinAll();
    printf("Pause\n");
    return getchar();
}

```

## 2.HttpClient
为了开发方面，我们使用OpenThread作为线程库。OpenThread可以实现多线程三大设计模式，开发这个HttpClient，使用Worker模式。
设计思路如下：
1. 每个HttpClient是一条线程OpenThreader。一个HttpClient对象，可以处理任意个Http请求。它由Factory类管理和创建。

2. 一次Http请求就是一个task，从Factory挑选一个HttpClient对象，向该对象发送task。并进行阻塞等待结果。

3. HttpClient对象收到task消息。把IP和端口做参数，调用OpenSocket的connect。
connect有两个作用，一个是执行网络连接产生一个fd，同时把这个fd加入到poll，fd与HttpClient对象的线程id进行绑定。
fd加入到poll成功以后，该socket的任何消息可以通过线程Id，发到对应的HttpClient。

4. HttpClient对象接收到socket的open消息后，向服务器发送http报文。

5. 因为connect把fd和线程id进行绑定。所以，HttpClient会收到服务器返回的Http报文。

6. HttpClient接收完Http报文，就唤醒请求线程。请求线程被唤醒，拿到Http请求数据。
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
//请求http对象，包含返回对象。
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
    //指定url，并进行解析和域名解析
    void setUrl(const std::string& url);
    inline void operator=(const std::string& url) { setUrl(url); }
    //http返回对象
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
        //解析返回http消息头
        void parseHeader();
        bool pushData(const char* data, size_t size);
    };
    HttpResponse response_;
    //阻塞当前线程，等待http消息返回，才继续执行。
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
//应用程序单利，封装OpenSocket，一个进程只有一个对象。
class App
{
    //Msg需要手动释放，放到智能指针，由智能指针释放
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
    //OpenSocket对象，可以设计成单利
    OpenSocket openSocket_;
    //App构造的时候，启动OpenSocket。
    App() {  openSocket_.run(App::SocketFunc); }
};
App App::Instance_;


////////////HttpClient//////////////////////
//HttpClient线程类，Factory管理一组线程。
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
    // name是线程名，必须制定。在Linux上，top -Hp可以看到这个线程名。
    HttpClient(const std::string& name)
        :OpenThreadWorker(name)
    {
        registers(SocketProto::ProtoType(), (OpenThreadHandle)&HttpClient::onSocketProto);
        registers(TaskProto::ProtoType(), (OpenThreadHandle)&HttpClient::onTaskProto);
        start();
    }
    ~HttpClient()
    {
        //销毁之前，尽可能唤醒请求线程，防止请求线程阻塞为被唤醒
        for (auto iter = mapFdToTask_.begin(); iter != mapFdToTask_.end(); iter++)
            iter->second.openSync_.wakeup();
    }

private:
    void onTaskProto(TaskProto& proto)
    {
        auto& request = proto.request_;
        //连接Http服务器，并把fd与当前线程绑定。该socket的全部消息，都发到此线程
        proto.fd_ = App::Instance_.openSocket_.connect(pid(), request->ip_, request->port_);
        request->response_.code_ = -1;
        request->response_.head_.clear();
        request->response_.body_.clear();
        mapFdToTask_[proto.fd_] = proto;
    }
    //与http服务器连接成功以后，发送http请求报文
    void onSendHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        //需要判断fd绑定的task是否存在，否则关闭与Http服务器的连接
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
        //制作好Http请求报文，发送给服务器。
        App::Instance_.openSocket_.send(task.fd_, buffer.data(), (int)buffer.size());
    }
    //处理Http服务器发送过了socket数据流，拼成完整的Http返回报文
    void onReadHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        //Http任务列表没有绑定fd的任务，就对该fd关闭。
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
    //与Http服务器关闭的消息，唤醒请求线程，并对fd绑定的任务，移出任务列表
    void onCloseHttp(const std::shared_ptr<OpenSocketMsg>& data)
    {
        auto iter = mapFdToTask_.find(data->fd_);
        if (iter != mapFdToTask_.end())
        {
            iter->second.openSync_.wakeup();
            mapFdToTask_.erase(iter);
        }
    }
    //接收绑定此线程的socket消息。
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
    //请求交易所的最新龙虎数据
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
    
    //域名解析，把域名转ip。可以缓存，提供效率
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

## 4.设计一个高并发服务器框架
Listener负责监听socket连接事件，发socket连接事件发给Accepter。

Accepter负责接收Listener发过来的socket连接事件，并与socket进行通信。

Client是客户端集群，使用它可以对服务器进行压力测试。

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