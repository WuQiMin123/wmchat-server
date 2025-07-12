#include "StatusGrpcClient.h"

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid){
    // 1.构造请求参数；
    ClientContext context;
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
	// 2.获取连接；
    auto stub = pool_->getConnection();
	// 3.调用远程方法；
    Status status = stub->GetChatServer(&context, request, &reply);
	// 4.RAII释放连接；
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });
	// 5.处理返回结果；
    if (status.ok()) {
        std::cout << "success get reply" << std::endl;
        return reply;
    }
    else {
        std::cout << "ErrorCodes::RPCFailed" << std::endl;
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }
}

StatusGrpcClient::StatusGrpcClient()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["StatusServer"]["Host"];
    std::string port = gCfgMgr["StatusServer"]["Port"];
    pool_.reset(new StatusConPool(5, host, port));
}