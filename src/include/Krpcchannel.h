#ifndef _Krpcchannel_h_
#define _Krpcchannel_h_

//此类是继承自google::protobuf::RpcChannel
//目的是为了给客户端进行方法调用的时候，统一接收的
#include <google/protobuf/service.h>
#include "zookeeperutil.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>


class KrpcChannel:public google::protobuf::RpcChannel
{
public:
    KrpcChannel(bool connectNow);
    ~KrpcChannel(){}
    void CallMethod(const ::google::protobuf::MethodDescriptor *method,
                    ::google::protobuf::RpcController *controller,
                    const ::google::protobuf::Message *request,
                    ::google::protobuf::Message *response,
                    ::google::protobuf::Closure *done)override;//override可以验证是否是虚函数
private:
    //存放客户端套接字
    int m_clientfd;
    std::string service_name;
    std::string m_ip;
    uint16_t m_port;
    std::string method_name;
    int m_idx; //用来划分服务器ip和port的下标
    bool newConnect(const char *ip,uint16_t port);
    std::string QueryServiceHost(ZkClient *zkclient,std::string service_name,std::string method_name,int &idx);
};
#endif