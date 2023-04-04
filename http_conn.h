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
  static const int READ_BUFFER_SIZE = 2048;
  static const int WRITE_BUFFER_SIZE = 1024;
  // HTTP请求放啊，但我们只支持GET
  // 默认情况下枚举值从0开始，然后递增
  enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

  /* 在解析客户端请求时，主状态机的状态
  CHECK_STATE_REQUESTLINE:当前正在分析请求行
  CHECK_STATE_HEADER:当前正在分析头部字段
  CHECK_STATE_CONTENT:当前正在解析请求体 */
  enum CHECK_STATE {CHECK_STATE_REQUESTLINE, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};

  // 从状态机，三种可能状态(即行的读取状态):
  // 0.读取到一个完整的行 2.行出错 3.行数据尚且不完整
  enum LINE_STATUS {LINE_OK, LINE_BAD, LINE_OPEN};

  /*服务器处理HTTP请求的可能结果，报文解析的结果
    NO_REQUEST:   请求不完整，需要继续读取客户数据
    GET_REQUEST:  标识获得了一个完整的客户请求
    BAD_REQUEST:  表示客户请求语法错误
    NO_RESOURCE:  表示服务器没有资源
    FORBIDDEN_REQUEST: 表示客户端对资源没有足够的访问权限
    FILE_REQUEST:      文件请求，获取文件成功
    INTERNAL_ERROR:    表示服务器内部错误
    CLOSED_CONNECTION: 表示客户端已经关闭连接
  */
  enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
                  INTERNAL_ERROR, CLOSED_CONNECTION};

  HttpConn() {}
  ~HttpConn() {}
  void Process();           // 解析客户端的请求报文(Proactor模式下)
  void Init(int sockfd, const sockaddr_in& addr);  // 初始化连接I/O相关信息
  void CloseConn();         // 关闭连接
  bool Read();              // 非阻塞读
  bool Write();             // 非阻塞写
  HTTP_CODE ProcessRead();        // 解析HTTP请求
  HTTP_CODE ParseRequestLine(char* text);   // 解析HTTP首行
  HTTP_CODE ParseHeader(char* text);        // 解析HTTP请求头     
  HTTP_CODE ParseContent(char* text);       // 解析HTTP主体
  LINE_STATUS ParseLine();                  // 解析一行(请求头或请求行)，并在末尾加上字符串结束符，方便提取
  char* GetLineAddr() {return read_buf_ + start_line_;} // 获取当前正在解析的行的地址
  HTTP_CODE DoRequest();
private:
  void Init();              // 初始化连接HTTP的相关信息
  int sockfd_ = -1;         // 当前个http连接的套接字
  sockaddr_in address_;     // 通信的socket地址
  char read_buf_[READ_BUFFER_SIZE];  // 读缓冲的大小
  int read_idx_ = 0;                 // 读缓冲区中已读入的客户端数据的最后一个字节的下一个字节(类似于PC)
  int checked_idx_;                  // 当前正在分析的字符在读缓冲区的位置
  int start_line_;                   // 正在解析的行的起始位置
  CHECK_STATE check_state_;          // 主状态机所处的状态
  // 请求体中的三个数据
  char* url_;                        // 请求目标文件的文件名
  char* version_;                    // 协议版本
  METHOD method_;                    // 请求方法
  // 请求行中的关键字
  char* host_;                       // 主机名
  bool is_linger_;                   // HTTP是否要保持TCP连接
};

#endif