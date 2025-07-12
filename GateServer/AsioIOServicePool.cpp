#include "AsioIOServicePool.h"
#include <iostream>

using namespace std;

AsioIOServicePool::AsioIOServicePool(std::size_t size)
    : _ioServices(size), _nextIOService(0){
    _works.reserve(size);

    // 为每个 io_context 创建工作守卫
    for (std::size_t i = 0; i < size; ++i) {
        auto executor = _ioServices[i].get_executor();
        _works.emplace_back(std::make_unique<Work>(executor));
    }

    // 启动线程池
    for (std::size_t i = 0; i < size; ++i) {
        _threads.emplace_back([this, i]() {
            _ioServices[i].run();
            });
    }
}

AsioIOServicePool::~AsioIOServicePool() {
    Stop();
    cout << "AsioIOServicePool destruct" << endl;
}

boost::asio::io_context& AsioIOServicePool::GetIOService() {
    auto& service = _ioServices[_nextIOService++];
    if (_nextIOService == _ioServices.size()) {
        _nextIOService = 0;
    }
    return service;
}

void AsioIOServicePool::Stop() {
    // 1. 停止所有 io_context
    for (auto& io : _ioServices) {
        io.stop();
    }

    // 2. 释放工作守卫
    for (auto& work : _works) {
        work.reset();
    }

    // 3. 等待线程结束
    for (auto& t : _threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}