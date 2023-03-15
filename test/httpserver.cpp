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
                printf("SocketFunc dispatch faild pid = %d\n", (int)msg->uid_);
        }
        else
        {
            delete msg;
        }
    }
public:
    static App Instance_;
    App() { OpenSocket::Start(App::SocketFunc); }
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
        listen_fd_ = OpenSocket::Instance().listen((uintptr_t)pid(), TestServerIp_, TestServerPort_, 64);
        if (listen_fd_ < 0)
        {
            printf("Listener::onStart faild listen_fd_ = %d\n", listen_fd_);
            assert(false);
        }
        OpenSocket::Instance().start((uintptr_t)pid(), listen_fd_);
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
        OpenSocket::Instance().close(pid_, accept_fd);
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
                OpenSocket::Instance().close(pid(), accept_fd);
                return;
            }
            auto& client = mapClient_[accept_fd];
            client.fd_ = accept_fd;
            client.addr_ = proto.addr_;
            OpenSocket::Instance().start(pid_, accept_fd);
        }
    }
    //GET /xx/xx HTTP/x.x
    void onReadHttp(const std::shared_ptr<OpenSocketMsg> msg)
    {
        auto iter = mapClient_.find(msg->fd_);
        if (iter == mapClient_.end())
        {
            OpenSocket::Instance().close(pid_, msg->fd_);
            return;
        }
        auto& request = iter->second;
        if (!request.pushData(msg->data(), msg->size()))
        {
            //Header too large.close connet.
            if (request.head_.size() > 1024)
                OpenSocket::Instance().close(pid_, msg->fd_);
            return;
        }
        printf("new client:url = %s\n", request.url_.c_str());
        std::string content;
        content.append("<div>It's work!</div><br/>" + request.addr_ + "request:" + request.url_);
        std::string buffer = "HTTP/1.1 200 OK\r\ncontent-length:" + std::to_string(content.size()) + "\r\n\r\n" + content;
        OpenSocket::Instance().send(msg->fd_, buffer.data(), (int)buffer.size());
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
                OpenSocket::Instance().close(pid_, msg->fd_);
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