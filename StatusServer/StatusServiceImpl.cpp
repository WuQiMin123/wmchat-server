#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <climits>
#include <boost/asio.hpp>

std::string generate_unique_string() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();

	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);

	return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
	std::string prefix("wqm status server has received :  ");
	const auto& server = getChatServer();
	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(generate_unique_string());
	insertToken(request->uid(), reply->token());
	return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
	// 1. 获取配置
	auto& cfg = ConfigMgr::Inst();

	// 2. 读取配置中的服务器列表
	auto server_list = cfg["chatservers"]["Name"];

	// 3. 按逗号分割字符串为多个服务器名
	std::vector<std::string> words;
	std::stringstream ss(server_list);
	std::string word;
	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}

	//4. 遍历每个服务器名，构造 ChatServer 对象并存入 _servers
	for (auto& word : words) {
		if (cfg[word]["Name"].empty()) {
			continue;
		}

		ChatServer server;
		server.port = cfg[word]["Port"];
		server.host = cfg[word]["Host"];
		server.name = cfg[word]["Name"];
		_servers[server.name] = server;
	}

}

ChatServer StatusServiceImpl::getChatServer() {

	//std::cout << "getChatServer()" << std::endl;
	
	std::lock_guard<std::mutex> guard(_server_mtx);

	// 默认选取 _servers 中的第一个服务器作为初始最小连接数服务器
	auto minServer = _servers.begin()->second;

	// 准备获取更新的连接数
	int new_count = 0; //

	// 从 Redis 中获取该服务器的连接数
	auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);
	if (count_str.empty()) {
		//不存在则默认设置为最大
		minServer.con_count = INT_MAX;
	}
	else {
		minServer.con_count = std::stoi(count_str);
		new_count = minServer.con_count; //
	}


	// 遍历所有服务器，查找连接数最少的服务器
	for (auto& server : _servers) {

		if (server.second.name == minServer.name) {
			continue;
		}

		auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
		if (count_str.empty()) {
			server.second.con_count = INT_MAX;
		}
		else {
			server.second.con_count = std::stoi(count_str);
			new_count = server.second.con_count;  //
		}

		if (server.second.con_count < minServer.con_count) {
			minServer = server.second;
		}
		std::cout << "Selected server: " << minServer.name
			<< ", connection count: " << minServer.con_count << std::endl;
	}

	// 更新连接数（原本设置连接数的代码在检测chatserver心跳部分）
	new_count++;
	auto new_count_str = std::to_string(new_count);
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, minServer.name, new_count_str);

	std::cout << "[getChatServer] Final selected server: " << minServer.name
		<< ", connection count: " << RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name) << std::endl;
	return minServer;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
	// 从请求中获取 UID 和 token
	auto uid = request->uid();
	auto token = request->token();

	// 构造 Redis 中 token 存储用的 key，例如 "utoken_1039"
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;

	// 准备接收 Redis 返回的 token 值
	std::string token_value = "";

	// 从 Redis 获取该 UID 对应的 token
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		reply->set_error(ErrorCodes::UidInvalid);
		return Status::OK;
	}
	
	// 校验客户端传入的 token 与 Redis 中存储的是否一致
	if (token_value != token) {
		reply->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}

	// 验证成功，返回成功状态
	reply->set_error(ErrorCodes::Success);
	reply->set_uid(uid);
	reply->set_token(token);
	return Status::OK;
}

//将用户的唯一标识 uid 和对应的登录凭证 token 以键值对的形式存储到 Redis 中，用于后续的身份验证和状态管理。
void StatusServiceImpl::insertToken(int uid, std::string token)
{
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	RedisMgr::GetInstance()->Set(token_key, token);
}

bool StatusServiceImpl::isServerAvailable(const ChatServer& server) {
	try {
		boost::asio::io_context io;
		boost::asio::ip::tcp::resolver resolver(io);
		boost::asio::ip::tcp::socket socket(io);

		// 解析 IP 或主机名
		auto endpoints = resolver.resolve(server.host, server.port);

		// 尝试连接第一个可用的 endpoint（会自动迭代尝试）
		boost::asio::connect(socket, endpoints);

		return true;
	}
	catch (std::exception& e) {
		std::cout << "[isServerAvailable] failed to connect " << server.name
			<< " (" << server.host << ":" << server.port << "), error: " << e.what() << std::endl;
		return false;
	}
}