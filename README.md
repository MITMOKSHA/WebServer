# WebServer
- 使用线程池+非阻塞socket+epoll(ET)+事件处理(模拟Proactor)的并发模型
- 用状态机解析HTTP请求报文，支持解析GET请求
- 经webbench压力测试可支持上万的并发连接进行数据交换

### 编译环境
- GNU Make 4.2.1
- gcc 9.4.0
- Ubuntu 20.0.4

### 后续会加入
- [ ] 异步日志库
- [ ] 定时器