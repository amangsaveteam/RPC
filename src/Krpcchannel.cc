#include "Krpcchannel.h"
#include "Krpcheader.pb.h"
#include "zookeeperutil.h"
#include "Krpcapplication.h"
#include "Krpccontroller.h"
#include "memory"
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "KrpcLogger.h"

std::mutex g_data_mutex; //全局互斥锁，用于保护共享数据的线程安全

//RPC调用的核心方法，负责将客户端的请求序列化并发送到服务端，同时接收服务端的响应
void KrpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor* method,
                            ::google::protobuf::RpcController *controller,
                            const ::google::protobuf::Message *request,
                            ::google::protobuf::Message *response,
                            ::google::protobuf::Closure *done)
{
    if(-1 == m_clientfd){//如果客户端socket未初始化成功
        //获取服务对象名和方法名
        const google::protobuf::ServiceDescriptor *sd  = method->service();
        service_name = sd->name();    //服务名
        method_name = method->name(); //方法名

        //客户端需要查询ZooKeeper,找到提供该服务的服务器地址
        ZkClient zkCli;
        zkCli.Start(); //连接ZooKeeper服务器
        std::string host_data = QueryServiceHost(&zkCli,service_name,method_name,m_idx);//查询服务地址
        m_ip = host_data.substr(0,m_idx); // 从查询结果中提取IP地址
        std::cout<<"ip: "<<m_ip<<std::endl;
        m_port = atoi(host_data.substr(m_idx + 1,host_data.size() - m_idx).c_str());// 从查询结果中提取端口号
        std::cout << "port: " << m_port << std::endl;

        //尝试连接服务器
        auto rt = newConnect(m_ip.c_str(),m_port);
        if(!rt)
        {
            LOG(ERROR)<<"connect server error";//连接失败记录错误日志
            return;
        }else{
            LOG(INFO)<<"connect server success";//连接成功，记录日志
        }
    }

    //将请求参数序列化为字符串，并计算其长度
    uint32_t args_size{};
    std::string args_str;
    if(request->SerializeToString(&args_str)){//序列化请求函数
        args_size = args_str.size();    //获取序列化后的长度
    }else{
        controller->SetFailed("serialize request fail"); //序列化失败，设置错误信息
        return;
    }
    // 定义RPC请求的头部信息
    Krpc::RpcHeader krpcheader;
    krpcheader.set_service_name(service_name);
    krpcheader.set_method_name(method_name);
    krpcheader.set_args_size(args_size); 

    //将RPC头部信息序列化为字符串，并计算其长度
    uint32_t header_size = 0;
    std::string rpc_header_str;
    if(krpcheader.SerializeToString(&rpc_header_str)){//序列化头部信息
        header_size = rpc_header_str.size(); //获取序列化后的长度    
    }else{
        controller->SetFailed("serialize rpc header error!"); //序列化失败，设置错误信息
        return;
    }

    //将头部长度和头部信息拼接成完整的RPC请求报文
    std::string send_rpc_str;
    {
        google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
        google::protobuf::io::CodedOutputStream coded_output(&string_output);
        coded_output.WriteVarint32(static_cast<uint32_t>(header_size));//写入长度
        coded_output.WriteString(rpc_header_str);//写入头部信息
    }
    send_rpc_str += args_str; //拼接请求参数

    //发送RPC请求到服务器
    if(-1 == send(m_clientfd,send_rpc_str.c_str(),send_rpc_str.size(),0)){
        close(m_clientfd); //发送失败
        char errtxt[512] = {};
        std::cout << "send error: "<<strerror_r(errno,errtxt,sizeof(errtxt))<<std::endl;//打印错误信息
        controller->SetFailed(errtxt); //设置错误信息
        return;
    }
    //接收服务器的响应
    char recv_buf[1024] = {0};
    int recv_size = 0;
    if(-1 == (recv_size = recv(m_clientfd,recv_buf,1024,0))){
        char errtxt[512] = {};
        std::cout<<"recv error"<<strerror_r(errno,errtxt,sizeof(errtxt))<<std::endl;
        controller->SetFailed(errtxt);
        return;
    }

    //将接收到的响应数据反序列化为response对象
    if(!response->ParseFromArray(recv_buf,recv_size))
    {
        close(m_clientfd);//反序列化失败，关闭socket
        char errtxt[512] = {};
        std::cout<<"parse error"<<strerror_r(errno,errtxt,sizeof(errtxt))<<std::endl;
        controller->SetFailed(errtxt);//设置错误信息
        return;
    }
    close(m_clientfd); //关闭socket连接
}

//创建新的socket连接
bool KrpcChannel::newConnect(const char *ip,uint16_t port){
    //创建socket
    int clientfd = socket(AF_INET,SOCK_STREAM,0);
    if(-1 == clientfd){
        char errtxt[512] = {0};
        std::cout<<"socket error"<<strerror_r(errno,errtxt,sizeof(errtxt))<<std::endl;
        LOG(ERROR)<<"socket error:"<<errtxt;
        return false;
    }
    //设置服务器地址信息
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;//ipv4地址族
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    //尝试连接服务器
    if(-1 == connect(clientfd,(struct sockaddr *)&server_addr,sizeof(server_addr))){
        close(clientfd);
        char errtxt[512] = {0};
        std::cout<<"connect error"<<strerror_r(errno,errtxt,sizeof(errtxt))<<std::endl;
        LOG(ERROR)<<"connect server error"<<errtxt;
        return false;
    }
    m_clientfd = clientfd; //保存socket文件描述符
    return true;
}

// 从ZooKeeper查询服务地址
std::string KrpcChannel::QueryServiceHost(ZkClient *zkclient,std::string service_name,std::string method_name,int &idx){
    std::string method_path = "/" + service_name + "/" + method_name;//构造ZooKeeper路径
    std::cout<<"method_path: "<<method_path<<std::endl;

    std::unique_lock<std::mutex>lock(g_data_mutex); //加锁，保证线程安全
    std::string host_data_1 = zkclient->GetData(method_path.c_str()); //从ZooKeeper中获取数据
    lock.unlock();

    if(host_data_1 == ""){//如果未找到服务地址
        LOG(ERROR)<<method_path + " is no exist!";
        return " ";
    }
    idx = host_data_1.find(":");
    if(-1 == idx)
    {
        LOG(ERROR)<<method_path + " address is valid!";
        return " ";
    }
    return host_data_1;
}
//构造函数，支持延迟连接
KrpcChannel::KrpcChannel(bool connectNow) : m_clientfd(-1),m_idx(0){
    if(!connectNow){
        //不需要立即连接
        return;
    }
    //尝试连接服务器，最多重试3次
    auto rt = newConnect(m_ip.c_str(),m_port);
    int count = 3;
    while(!rt && count--)
    {
        rt = newConnect(m_ip.c_str(),m_port);
    }
}