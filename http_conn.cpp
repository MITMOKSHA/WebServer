#include "http_conn.h"


int HttpConn::epollfd_ = -1;
int HttpConn::user_count_ = 0;

void HttpConn::Process() {
  // 处理业务逻辑
  // TODO:解析HTTP请求
  printf("正在解析HTTP请求并响应...\n");
  // TODO:生成响应
}

// 设置文件描述符非阻塞
int SetNonBlocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = O_NONBLOCK | old_option;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void Addfd(int epollfd, int fd, bool one_shot) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;  // EPOLLRDHUP检测对端套接字是否关闭
  // 对于注册了EPOLLONESHOT事件的fd，os最多触发其上的一个可读、可写或异常事件，且只触发一次
  if (one_shot) {  // 保证同一时刻只有一个线程在操纵一个socket
    event.events |= EPOLLONESHOT;
  }
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);  // 将事件加入注册到内核事件表中
  SetNonBlocking(fd);
}

void Delfd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
  close(fd);
}

// 第三个参数是事件
void Modfd(int epollfd, int fd, int ev) {
  epoll_event event;
  event.data.fd = fd;
  event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void HttpConn::Init(int sockfd, const sockaddr_in& addr) {
  printf("有新的客户端%d进来了\n", sockfd);
  sockfd_ = sockfd;
  address_ = addr;

  // 端口复用
  int reuse = 1;
  setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // 将与客户端连接的connfd加入内核事件表中
  Addfd(epollfd_, sockfd_, true);
  user_count_++;
}

void HttpConn::CloseConn() {
  if (sockfd_ >= 0)  { 
    Delfd(epollfd_, sockfd_);  // 从内核事件表中删除fd
    sockfd_ = -1;
    user_count_--;  // 减少总的用户数
  }
}

bool HttpConn::Read() {
  // TODO:读
  printf("一次性读完数据\n");
  Modfd(epollfd_, sockfd_, EPOLLOUT);
  return true;
}
bool HttpConn::Write() {
  // TODO:写
  printf("一次性写完数据\n");
  return true;
}