#ifndef __Krpcapplication_H
#define __Krpcapplication_H
#include "Krpcconfig.h"
#include "Krpcchannel.h"
#include "Krpccontroller.h"
#include <mutex>
//Krpc基础类 ，负责框架的一些初始化操作
class KrpcApplication{
public:
    static void Init(int argc,char **argv);
    static KrpcApplication & GetInstance();
    static void deleteInstance();
    static Krpcconfig& GetConfig();
private:
    static Krpcconfig m_config;
    //全局唯一单例访问对象
    static KrpcApplication *m_application;
    static std::mutex m_mutex;
    KrpcApplication(){}
    ~KrpcApplication(){}
    KrpcApplication(const KrpcApplication&)=delete;
    KrpcApplication(KrpcApplication&&)=delete;
};
#endif