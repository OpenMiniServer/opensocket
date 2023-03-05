#ifndef WORKER_HEADER_H
#define WORKER_HEADER_H

#include <assert.h>
#include "opensocket.h"
#include "openthread.h"
#include <map>
#include <set>
#include <memory>
#include <stdlib.h>
#include <string.h>

using namespace open;

enum EProtoType
{
    EProtoText,
    EProtoSocket
};

class Hashid 
{
    struct Node
    {
        int id_;
        Node* next_;
    };
    int hashmod_;
    int cap_;
    int count_;
    Node* id_;
    Node** hash_;
public:
    Hashid()
        :id_(0),
        hash_(0),
        cap_(0),
        count_(0),
        hashmod_(0)
    {}
    ~Hashid()
    {
        clear();
    }
    void clear()
    {
        if (id_)
        {
            delete[] id_;
            id_ = 0;
        }
        if (hash_)
        {
            delete[] hash_;
            hash_ = 0;
        }
    }
    void init(int max) 
    {
        clear();
        int hashcap = 16;
        while (hashcap < max) {
            hashcap *= 2;
        }
        hashmod_ = hashcap - 1;
        cap_ = max;
        count_ = 0;
        id_ = new Node[max];
        for (int i = 0; i < max; i++) {
            id_[i].id_   = -1;
            id_[i].next_ = NULL;
        }
        hash_ = new Node*[hashcap];
        memset(hash_, 0, hashcap * sizeof(Node*));
    }

    int lookup(int id) 
    {
        int h = id & hashmod_;
        Node* c = hash_[h];
        while (c) {
            if (c->id_ == id)
                return (int)(c - id_);
            c = c->next_;
        }
        return -1;
    }

    int remove(int id) 
    {
        int h = id & hashmod_;
        Node* c = hash_[h];
        if (c == NULL)
            return -1;
        if (c->id_ == id) {
            hash_[h] = c->next_;
            goto _clear;
        }
        while (c->next_) {
            if (c->next_->id_ == id) {
                Node* temp = c->next_;
                c->next_ = temp->next_;
                c = temp;
                goto _clear;
            }
            c = c->next_;
        }
        return -1;
    _clear:
        c->id_ = -1;
        c->next_ = NULL;
        --count_;
        return (int)(c - id_);
    }

    int insert(int id) 
    {
        Node* c = NULL;
        for (int i = 0; i < cap_; ++i) {
            int index = (i + id) % cap_;
            if (id_[index].id_ == -1) {
                c = &id_[index];
                break;
            }
        }
        assert(c);
        ++count_;
        c->id_ = id;
        assert(c->next_ == NULL);
        int h = id & hashmod_;
        if (hash_[h]) {
            c->next_ = hash_[h];
        }
        hash_[h] = c;
        return (int)(c - id_);
    }

    inline int full() 
    {
        return count_ == cap_;
    }
};


class Data
{
    int srcPid_;
    EProtoType ptype_;
    std::string srcName_;
    std::string key_;
    std::shared_ptr<const void> proto_;
    Data(const Data& that) {}
    void operator=(const Data& that) {}
public:
    Data() :srcPid_(-1), ptype_(EProtoText) {}
    Data(int pid, const std::string& name, const std::string& key)
        :srcPid_(pid), srcName_(name), key_(key) {}
    ~Data() { }

    inline const int& srcPid() const { return srcPid_; }
    inline const EProtoType& ptype() const { return ptype_; }
    inline const std::string& srcName() const { return srcName_; }
    inline const std::string& key() const { return key_; }

    template <class T>
    inline const T* proto() const 
    { 
        return dynamic_cast<const T*>((const T*)proto_.get());
    }

    inline void setProto(EProtoType ptype, const std::shared_ptr<const void>& proto) { ptype_ = ptype; proto_ = proto; }

};

class Worker;
typedef void(Worker::* Handle)(const Data&);
struct Rpc
{
    Handle handle_;
};

class Worker : public OpenThreader
{
public:
    Worker(const std::string& name)
        :OpenThreader(name)
    {}
    virtual ~Worker() {}

    template <class T>
    bool send(int sid, const std::string& key, const T& t)
    {
        printf("[%s]send=>[%s] key:%s\n", name_.c_str(), ThreadName(sid).c_str(), key.c_str());
        auto data = std::shared_ptr<Data>(new Data(pid(), name_, key));
        auto proto = std::shared_ptr<const T>(new const T(t));
        data->setProto(EProtoText, proto);
        bool ret = OpenThread::Send(sid, data);
        assert(ret);
        return ret;
    }
    void onText(const Data& data)
    {
        printf("[%s]receive<<=[%s] key:%s\n", name_.c_str(), data.srcName().c_str(), data.key().c_str());
        auto iter = mapKeyFunc_.find(data.key());
        if (iter != mapKeyFunc_.end())
        {
            auto& rpc = iter->second;
            if (rpc.handle_)
            {
                (this->*rpc.handle_)(data); return;
            }
        }
        printf("[%s]no implement key:%s\n", name_.c_str(), data.key().c_str());
    }
    virtual void onSocket(const Data& data)
    {
    }
    virtual void onMsg(OpenThreadMsg& msg)
    {
        const Data* data = msg.data<Data>();
        if (data)
        {
            if (data->ptype() == EProtoText)
                onText(*data);
            else if (data->ptype() == EProtoSocket)
                onSocket(*data);
        }
    }
    std::map<std::string, Rpc> mapKeyFunc_;
};

#endif //WORKER_HEADER_H