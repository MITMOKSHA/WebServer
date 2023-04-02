#ifndef HTTPCONN_H_
#define HTTPCONN_H_

#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/epoll.h>

#include "locker.h"

class HttpConn {
public:
  static int epollfd_;  // 所有的socket上的事件都被注册到同一个epollfd指向的内核事件表中
  static int user_count_;  // 统计用户的数量
  HttpConn() {

  }
  ~HttpConn() {

  }
  void Process();           // 解析客户端的请求报文(Proactor模式下)
  void Init(int sockfd, const sockaddr_in& addr);  // 初始化连接
  void CloseConn();         // 关闭连接
  bool Read();              // 非阻塞读
  bool Write();             // 非阻塞写
private:
  int sockfd_ = -1;         // 当前个http连接的套接字
  sockaddr_in address_;     // 通信的socket地址
};

#endif