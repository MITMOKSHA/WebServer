#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <exception>
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535             // 最大的文件名描述符个数
#define MAX_EVENT_NUM 10000      // epoll最大监听事件数量

static int pipefd[2];  // 用于读写信号的管道

// extern声明http_conn中定义的函数
// 从epoll中删除文件描述符
extern void Delfd(int epollfd, int fd);

// 添加文件描述符到epoll中
extern void Addfd(int epollfd, int fd, bool one_shot, bool et);

// 修改文件描述符，重置socket上EPOLLONESHOT事件(只触发一次)，以确保下一次可读时，EPOLLIN事件被触法
extern void Modfd(int epollfd, int fd, int ev);

extern int SetNonBlocking(int fd);

extern void TimerHandler();

void SigHandler(int sig) {
  int save_errno = errno;
  int msg = sig;
  send(pipefd[1], (char*)&msg, 1, 0);
  errno = save_errno;
}

// 添加信号捕捉
void AddSig(int sig, void(handler)(int)) {
  struct sigaction sa;
  bzero(&sa, sizeof(sa));     // 初始化
  // handler可以指定为SIG_IGN和SIG_DFL
  sa.sa_handler = handler;    // 设置信号处理函数
  sa.sa_flags |= SA_RESTART;  // 系统调用被中断后可以自动重启
  sigfillset(&sa.sa_mask);    // 设置信号集中的所有信号(即调用处理程序时不允许其他信号中断处理程序的执行)
  // 对sig信号绑定一个sigaction结构体(指定处理函数sa_handler和执行处理程序时阻塞的信号集sa_mask)
  sigaction(sig, &sa, NULL);  
}

// 模拟Proactor模式(主线程监听并读数据)
// main函数中执行的就是主线程
int main(int argc, char** argv) {
  if (argc <= 1) {
    printf("请按照如下格式运行：%s 端口号\n", basename(argv[0]));
    exit(-1);
  }
  int port = atoi(argv[1]);

  // 创建线程池，并初始化
  ThreadPool<HttpConn>* pool = NULL;
  try {
    pool = new ThreadPool<HttpConn>;
  } catch(...) {  // (...)表示处理任何类型的异常
    exit(-1);
  }

  // 创建一个数组用于保存所有的客户信息
  HttpConn* users = new HttpConn[MAX_FD];

  // 创建监听的套接字
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket error\n");
    exit(-1);
  }

  // 设置端口复用，使用SO_REUSEADDR来强制使用被处于TIME_WAIT状态的连接占用的socket地址 
  // 固定写法
  int reuse = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  
  // 绑定服务器的端口和IP地址(唯一标识一台主机上的一个进程)
  sockaddr_in server_addr;
  server_addr.sin_port = htons(port);
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (ret != 0) {
    perror("bind error\n");
    exit(-1);
  }

  // 监听
  ret = listen(listenfd, 5);
  if (ret != 0) {
    perror("listen error\n");
    exit(-1);
  }

  // 创建epoll对象，事件数组，添加监听的文件描述符
  epoll_event events[MAX_EVENT_NUM];
  int epollfd = epoll_create(5);
  if (epollfd < 0) {
    perror("epoll create error\n");
    exit(-1);
  }

  // 创建管道
  ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
  assert(ret != -1);
  // 将管道写端fd置为非阻塞，并将读端fd加入epoll内核事件表中
  SetNonBlocking(pipefd[1]);
  Addfd(epollfd, pipefd[0], false, false);
  // 将监听的文件描述符添加到epoll对象中
  Addfd(epollfd, listenfd, false, false);

  // 若网路对端断开了，还往对端去写数据，会产生SIGPIPE(需要进行处理)
  // 对SIGPIPE进行处理
  AddSig(SIGPIPE, SIG_IGN);  // 因为SIGPIPE默认情况下会终止进程，直接忽略(设置为SIG_IGN)
  AddSig(SIGALRM, SigHandler);
  AddSig(SIGTERM, SigHandler);
  // 是否终止服务器的运行
  bool stop_server = false;
  bool timeout = false;
  alarm(HttpConn::timeslot_);

  HttpConn::epollfd_ = epollfd;

  while (!stop_server) {
    // num为就绪的事件数
    int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);  // 主线程注册就绪事件
    if (num < 0 && errno != EINTR)  {
      perror("epoll failure\n");
      break;
    }
    // 循环遍历事件数组
    for (int i = 0; i < num; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {
        // 有客户端连接进来
        // 客户端的socket地址结构，调用accept后会将数据写入到该数据结构
        sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addrlen);
        if (connfd < 0) {
          perror("accept error\n");
          exit(-1);
        }
        if (HttpConn::user_count_ >= MAX_FD) {
          // 目前的连接数满了，给客户端提示信息：服务器正忙
          close(connfd);
          printf("服务器正忙\n");
          continue;
        }
        // 将新的客户的数据初始化，放到数组中
        users[connfd].Init(connfd, client_addr);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
        // 对方异常断开或者错误等事件
        users[sockfd].CloseConn();
      } else if (events[i].events & EPOLLOUT) {
        if (!users[sockfd].Write()) {  // 一次性写完所有数据
          users[sockfd].CloseConn();   // 关闭当前socket释放资源
        }
      } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
        // 处理信号(管道读端有数据)
        // int sig;  // linux标准信号编号为1~31用一个整型32bit就可以通过掩码区分出具体的信号了
        char signals[1024];  // 读缓冲区的大小
        ret = recv(pipefd[0], signals, sizeof(signals), 0);
        if (ret == -1) {
          // 错误
          continue;
        } else if (ret == 0) {
          // 对端关闭
          continue;
        } else {
          for (int i = 0; i < ret; ++i) {
            // 每个信号值占一个字节(1~31)
            switch (signals[i]) {
              case SIGALRM: {
                // 超时(这里只是标记以下并不马上处理)
                timeout = true;
                break;
              }
              case SIGTERM: {
                // 终止server事件循环的运行
                stop_server = true;
              }
            }
          }
        }
      } else if (events[i].events & EPOLLIN) {
        // 模拟Preactor模式，由主线程来处理I/O，工作线程处理业务逻辑(Process)
        if (users[sockfd].Read()) {     
          // 一次性把所有数据都读完
          pool->Append(&users[sockfd]);  // 主线程将事件放入请求队列交给工作线程中
        } else {
          users[sockfd].CloseConn();
        }
      }
    }
    if (timeout) {
      TimerHandler();
      timeout = false;
    }
  }
  // 释放所有资源
  close(epollfd);
  close(listenfd);
  delete[] users;
  delete pool;
  return 0;
}