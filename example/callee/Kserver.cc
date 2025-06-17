//服务器的主函数
#include <iostream>
#include <string>
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "Krpcprovider.h"

//UserService 原本是一个本地服务，提供了两个本地方法：Login和GetFriendLists
//现在通过RPC框架，这些方法可以被远程调用
//继承自protobuf生成的RPC 服务基类
class UserService : public Kuser::UserServiceRpc
{
public:
    //本地登陆方法，用于处理实际的业务逻辑
    bool Login(std::string name,std::string pwd)
    {
        std::cout<<"doing local service Login"<<std::endl;
        std::cout<<"name:"<<name<<" pwd :"<<pwd<<std::endl;
        return true;
    }
    /*
    重写基类UserServiceRpc的虚函数，这些方法会被RPC框架直接调用
    1.调用者（caller）通过RPC框架发送Login请求
    2.服务提供者（callee）接受到请求后，调用下面重写Login的方法
    */
   void Login(::google::protobuf::RpcController* controller,
            const ::Kuser::LoginRequest* request,
            ::Kuser::LoginResponse* response,
            ::google::protobuf::Closure* done){
            //从请求中获取用户名和密码
            std::string name = request->name();
            std::string pwd = request->pwd();

            //调用本地业务逻辑处理登陆
            bool login_result = Login(name,pwd);
            
            //将响应结果写入response对象
            Kuser::ResultCode *code = response->mutable_result();
            code->set_errcode(0); //设置错误码为 0 ，表示成功
            code->set_errmsg(""); //设置错误信息为空
            response->set_success(login_result); //设置登陆结果

            //执行回调操作，框架会自动将响应序列化并发送给调用者
            done->Run();
            }
    
};
int main(int argc,char **argv){
    //调用框架的初始化操作，解析命令行参数并加载配置文件
    KrpcApplication::Init(argc,argv);

    //创建一个RPC服务提供者对象
    KrpcProvider provider;

    //将UserService 对象发布到RPC节点上，是其可以被远程调用
    provider.NotifyService(new UserService());

    //启动RPC服务节点，进入阻塞状态，等待远程RPC的调用请求
    provider.Run();
    return 0;
}