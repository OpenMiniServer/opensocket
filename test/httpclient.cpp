#include <assert.h>
#include <time.h>
#include <math.h>
#include <map>
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