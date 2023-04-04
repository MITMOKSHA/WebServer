#include "http_conn.h"


int HttpConn::epollfd_ = -1;
int HttpConn::user_count_ = 0;

#define TEST 0  // 测试LOG宏


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
  event.events |= EPOLLET;              // 使用ET模式
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
  event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void HttpConn::Process() {
  // 处理业务逻辑
  // TODO:解析HTTP请求
  HTTP_CODE read_ret = ProcessRead();
  if (read_ret = NO_REQUEST) {  // 请求不完整需要继续读取客户数据
     Modfd(epollfd_, sockfd_, EPOLLIN);
     return;
  }
  printf("正在解析HTTP请求并响应...\n");
  // TODO:生成响应
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
  
  Init();
}

void HttpConn::Init() {
  bzero(read_buf_, sizeof(read_buf_));
  check_state_ = CHECK_STATE_REQUESTLINE;
  checked_idx_ = 0;
  start_line_ = 0;
  read_idx_ = 0;
  url_ = 0;
  version_ = 0;
  method_ = GET;
  host_ = 0;
  is_linger_ = false;
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
  if (read_idx_ >= READ_BUFFER_SIZE) {
    return false;
  }
  // 读取到的字节
  int bytes_read = 0;
  while (true) {  // ET模式要一直读
    bytes_read = recv(sockfd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 没有数据
        break;  // 退出读循环
      }
      return false;  // 出错了
    } else if (bytes_read == 0) {
      // 对方关闭连接
      return false;
    }
    read_idx_ += bytes_read;
  }
  printf("读取到了数据：%s\n", read_buf_);
  // Modfd(epollfd_, sockfd_, EPOLLOUT);
  return true;
}
bool HttpConn::Write() {
  // TODO:写
  printf("一次性写完数据\n");
  return true;
}

// 主状态机，解析请求
// 枚举类型在静态区域(共享)，因此要加上类作用域的限定
HttpConn::HTTP_CODE HttpConn::ProcessRead()
{
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char* text = 0;
  // 一行一行进行解析
  // 在请求体中就不需要调用ParseLine去解析一行了
  while ((check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
        (line_status = ParseLine()) == LINE_OK) {
    // 获取一行数据
    text = GetLineAddr();
    start_line_ = checked_idx_;   // 调用ParseLine()后checked_idx_会自动更新
    printf("Got 1 http line : %s\n", text);
    switch (check_state_) {
      case CHECK_STATE_REQUESTLINE: {
        ret = ParseRequestLine(text);
        #if TEST
          // 测试效果
          printf("method: %d\n", method_);
          printf("url: %s\n", url_);
          printf("version: %s\n", version_);
        #endif
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        } else {
          break;
        }
      }
      case CHECK_STATE_HEADER: {
        ret = ParseHeader(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        } else if (ret == GET_REQUEST) {
          return NO_REQUEST;
          // return ResolveRequest();  // 解析具体的信息
        } else {
          break;
        }
      }
      case CHECK_STATE_CONTENT: {
        if (ret == GET_REQUEST) {
          // return ResolveRequest();  // 解析具体的信息
          return NO_REQUEST;
        } else {
          line_status = LINE_OPEN;
          break;
        }
      }
      default: {
        return INTERNAL_ERROR;
      }
    }
  }
  return NO_REQUEST;  // 主状态机置为初始状态
}

// 解析HTTP请求行，获得请求方法，目标URL，HTTP版本
HttpConn::HTTP_CODE HttpConn::ParseRequestLine(char *text)
{
  // GET /index.html HTTP/1.1
  // strpbrk返回字符空格或字符\t在text先出现的位置
  url_ = strpbrk(text, " \t");
  if (!url_) {  // return NULL表示该子字符在字符串中找不到
    return BAD_REQUEST;
  }
  // GET\0/index.html HTTP/1.1
  *url_++ = '\0';
  char* method = text;  // 得到请求方法(因为遇到字符串结束符)
  // strcasecmp大小写不敏感
  // 这个版本只解析GET
  if (strcasecmp(method, "GET") == 0) {
    method_ = GET;
  } else {
    return BAD_REQUEST;
  }
  // /index.html HTTP/1.1
  version_ = strpbrk(url_, " \t");
  if (!version_) {
    return BAD_REQUEST;
  }
  // /index.html\0HTTP/1.1
  *version_++ = '\0';
  if (strcasecmp(version_, "HTTP/1.1") != 0) {
    return BAD_REQUEST;
  }
  // http://192.168.1.1:10000/index.html  有的url是这种类型的
  if (strncasecmp(url_, "http://", 7) == 0) {
    // 192.168.1.1:10000/indel.html
    url_ += 7;
    // /indel.html
    url_ = strchr(url_, '/');  // 和strpbrk的功能类似，不过只能检测一个字符
  }
  
  // /index.html
  if (!url_ || url_[0] != '/') {
    return BAD_REQUEST;
  }
  
  check_state_ = CHECK_STATE_HEADER;  // 主状态机变成检查请求头
  return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseHeader(char *text)
{
  return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::ParseContent(char *text)
{
  return HTTP_CODE();
}

// 解析一行，判断条件为/r/n
HttpConn::LINE_STATUS HttpConn::ParseLine()
{
  char temp;
  for (;checked_idx_ < read_idx_; ++checked_idx_) {
    temp = read_buf_[checked_idx_];
    if (temp == '\r') {
      if (checked_idx_ + 1 == read_idx_) {
        return LINE_OPEN;
      } else if (read_buf_[checked_idx_+1] == '\n') {
        read_buf_[checked_idx_++] = '\0';  // 换位字符串结束符，读取字符串
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    } else if (temp == '\n') {
      // 这种情况保证，若上一次读到的最后一个字符为'\r'但并没有读完，这一次也得进行判断
      if (checked_idx_ > 1 && read_buf_[checked_idx_-1] == '\r') {
        read_buf_[checked_idx_++] = '\0';  // 换位字符串结束符，读取字符串
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  return LINE_OPEN;
}

HttpConn::HTTP_CODE HttpConn::DoRequest()
{
  return HTTP_CODE();
}
