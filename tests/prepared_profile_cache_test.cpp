#include "PreparedProfileCache.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir dir;
    const auto check=[](bool ok,const char *why){if(!ok)std::cerr<<"FAILED: "<<why<<'\n';return ok;};
    const auto write=[](const QString &p,const QByteArray &b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();};
    const auto a=dir.filePath("a.dcp"),b=dir.filePath("b.lcp"),c=dir.filePath("c.dcp");
    if(!dir.isValid()||!write(a,"AA")||!write(b,"BB")||!write(c,"CC"))return 1;
    std::atomic<int> parses{0};
    const PreparedProfileCache::Loader loader=[&](const QByteArray &bytes,QString *) {
        ++parses;return PreparedProfileCache::Loaded{std::make_shared<const QByteArray>(bytes),256};
    };
    const auto get=[&](const QString &p){return std::static_pointer_cast<const QByteArray>(PreparedProfileCache::acquire(p,"test-parser-1",loader,nullptr));};
    PreparedProfileCache::configure(600,2);
    const auto heldA=get(a),heldB=get(b);
    if(!check(heldA&&heldB&&get(a)==heldA&&parses==2,"immutable hit shares prepared object"))return 1;
    if(!get(c))return 1;
    auto stats=PreparedProfileCache::statistics();
    if(!check(stats.entries==2&&stats.retainedBytes==512&&stats.evictions==1&&*heldB=="BB",
        "LRU eviction keeps active readers valid"))return 1;
    if(!get(b)||!check(parses==4,"evicted object is prepared again"))return 1;
    const auto time=QFileInfo(a).lastModified();
    if(!write(a,"AX"))return 1;
    QFile sameTime(a);
    if(!sameTime.open(QIODevice::ReadWrite)||!sameTime.setFileTime(time,QFileDevice::FileModificationTime))return 1;
    sameTime.close();
    if(!check(*get(a)=="AX"&&*heldA=="AA","same size/time mutation cannot reuse stale bytes"))return 1;
    if(!QFile::remove(a)||!check(!get(a),"deleted source cannot be served from cache"))return 1;
    PreparedProfileCache::configure(128,2);parses=0;
    if(!get(b)||!get(b)||!check(parses==2&&PreparedProfileCache::statistics().entries==0,"oversized profile bypasses retention"))return 1;
    PreparedProfileCache::configure(1024,0);
    if(!get(b)||!check(PreparedProfileCache::statistics().entries==0,"zero-entry cache disabled"))return 1;
    PreparedProfileCache::configure(1024,4);parses=0;
    const PreparedProfileCache::Loader slow=[&](const QByteArray &bytes,QString *) {
        ++parses;std::this_thread::sleep_for(std::chrono::milliseconds(40));
        return PreparedProfileCache::Loaded{std::make_shared<const QByteArray>(bytes),256};
    };
    std::vector<std::future<std::shared_ptr<const void>>> requests;
    for(int i=0;i<8;++i)requests.push_back(std::async(std::launch::async,[&]{return PreparedProfileCache::acquire(b,"test-parser-1",slow,nullptr);}));
    std::shared_ptr<const void> shared;
    for(auto &r:requests){auto value=r.get();if(!shared)shared=value;if(!check(value&&value==shared,"concurrent callers share preparation"))return 1;}
    if(!check(parses==1&&PreparedProfileCache::statistics().preparations==1,"single-flight count"))return 1;
    const PreparedProfileCache::Loader broken=[](const QByteArray &,QString *) -> PreparedProfileCache::Loaded {throw std::runtime_error("synthetic failure");};
    QString error;
    if(!check(!PreparedProfileCache::acquire(c,"broken-parser",broken,&error)&&!error.isEmpty(),"parser failure wakes callers"))return 1;
    if(!check(get(c)!=nullptr,"failure does not poison another parser"))return 1;
    PreparedProfileCache::clear();
    std::promise<void> entered,release;const auto gate=release.get_future().share();
    auto active=std::async(std::launch::async,[&]{return PreparedProfileCache::acquire(b,"blocked-parser",[&](const QByteArray &bytes,QString *){
        entered.set_value();gate.wait();return PreparedProfileCache::Loaded{std::make_shared<const QByteArray>(bytes),256};
    },nullptr);});
    const bool started=entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready;
    PreparedProfileCache::clear();release.set_value();
    if(!check(active.get()&&started&&PreparedProfileCache::statistics().entries==0,"clear prevents old generation refill"))return 1;
    PreparedProfileCache::configure(64ULL*1024*1024,16);
    std::cout<<"Prepared cache budget, LRU, content invalidation, concurrency and failure tests passed\n";
    return 0;
}
