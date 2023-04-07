#include "http_conn.h"

#define TEST 0  // 测试LOG宏
#define LOG 1   // LOG宏


int HttpConn::epollfd_ = -1;
int HttpConn::user_count_ = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on thi server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
// 网站根目录
const char* doc_root = "/home/moksha/webserver/resources";  // 会自动加上字符串结束符

// 设置文件描述符非阻塞
int SetNonBlocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = O_NONBLOCK | old_option;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void Addfd(int epollfd, int fd, bool one_shot, bool et) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;  // EPOLLRDHUP检测对端套接字是否关闭
  if (et) {
    event.events |= EPOLLET;              // 使用ET模式
  }
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

void HttpConn::Init(int sockfd, const sockaddr_in& addr) {
  printf("有新的客户端%d进来了\n", sockfd);
  sockfd_ = sockfd;
  address_ = addr;

#if LOG  // 便于调试
  // 端口复用
  int reuse = 1;
  setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

  // 将与客户端连接的connfd加入内核事件表中
  Addfd(epollfd_, sockfd_, true, true);
  user_count_++;
  
  Init();
}

void HttpConn::Init() {
  check_state_ = CHECK_STATE_REQUESTLINE;
  checked_idx_ = 0;
  start_line_ = 0;
  read_idx_ = 0;
  write_idx_ = 0;
  url_ = 0;
  version_ = 0;
  method_ = GET;
  host_ = 0;
  port_ = 0;
  is_linger_ = false;
  content_length_ = 0;
  bzero(read_buf_, READ_BUFFER_SIZE);
  bzero(write_buf_, WRITE_BUFFER_SIZE);
  bzero(real_file_, FILENAME_LEN);
}

void HttpConn::CloseConn() {
  if (sockfd_ >= 0)  { 
    Delfd(epollfd_, sockfd_);  // 从内核事件表中删除fd
    sockfd_ = -1;
    user_count_--;  // 减少总的用户数
  }
}

bool HttpConn::Read() {
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
    } else if (bytes_read == 0) {  // EOF
      // 对方关闭连接
      return false;
    }
    read_idx_ += bytes_read;
  }
#if LOG
  printf("\n读取到了HTTP请求报文:\n%s", read_buf_);
#endif
  return true;
}

bool HttpConn::Write() {
  int bytes_num = 0;                 // 记录writev返回的写入的字节数
  int bytes_have_send = 0;           // 已经从写缓冲中发送给客户端的字节数
  int bytes_to_send = write_idx_;    // 写缓冲中的未发送数据(即未写入fd)的字节数
  if (bytes_to_send == 0) {           
    // 写缓冲中无数据，说明服务器端并没有检测到争取的HTTP请求报文，又因为ET模式因此需要重新注册读就绪事件，并重新初始化连接
    Modfd(epollfd_, sockfd_, EPOLLIN);
    Init();   
    return true;
  }
  while (1) {
    // 成功时writev返回写入的字节数
    bytes_num = writev(sockfd_, iv_, iv_count_);  // 将多块分散的内存写入连接fd中
    if (bytes_num == -1) {  // 返回-1为error
      if (errno == EAGAIN) {
      // 如果TCP写缓冲没有时间，则等待下一轮EPOLLOUT事件
        Modfd(epollfd_, sockfd_, EPOLLOUT);
        return true;
      }
      unmap();  // 对资源文件映射到内存的部分进行释放
      return false;
    }
    // 更新变量
    bytes_have_send += bytes_num;
    if (bytes_to_send <= bytes_have_send) {
      //TODO:
      // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
      unmap();           // 释放资源文件映射的内存空间
      if (is_linger_) {  // 如果是长连接，就初始化当前连接
        Init();   // 重新初始化当前连接
        Modfd(epollfd_, sockfd_, EPOLLIN);
        return true;
      } else {
        Modfd(epollfd_, sockfd_, EPOLLIN);
        return false;  // 短连接则关闭当前socket释放资源
      }
    }
  }
}

void HttpConn::Process() {
  // 处理业务逻辑
  HTTP_CODE read_ret = ProcessRead();
  #if TEST
    // 测试效果
    printf("\n=============test info==============\n");
    printf("method: %d\n", method_);
    printf("url: %s\n", url_);
    printf("version: %s\n", version_);
    printf("host_: %s\n", host_);
    printf("port_: %s\n", port_);
    printf("Linger: %d\n", is_linger_);
    printf("Content length : %d\n", content_length_);
    printf("====================================\n");
  #endif
  if (read_ret == NO_REQUEST) {  // 请求报文中的数据不完整需要继续读取客户数据
     Modfd(epollfd_, sockfd_, EPOLLIN);
     return;
  }
  // 根据解析HTTP请求报文得到的结果，进行生成响应报文
  bool write_ret = ProcessWrite(read_ret);
  if (!write_ret) {
    CloseConn();
  } else {
    Modfd(epollfd_, sockfd_, EPOLLOUT);  // 响应报文生成后工作线程注册写就绪事件
  }
}

// 主状态机，解析请求
// 枚举类型在静态区域(共享)，因此要加上类作用域的限定
HttpConn::HTTP_CODE HttpConn::ProcessRead()
{
  LINE_STATUS line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char* text = 0;
  // 一行一行进行解析
  // 在请求实体中就不需要调用ParseLine去解析一行了
  while ((check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
        (line_status = ParseLine()) == LINE_OK) {
    // text = read_buf_ + start_line;
    text = GetLineAddr();         // 获取当前正在解析行的地址(即第一个字节的位置)
    start_line_ = checked_idx_;   // 调用ParseLine()成功后，checked_idx_会更新为下一行报文的起始地址(相当于预取)
    // 第一次执行的时候状态机处于初始化状态(即处于请求头状态)
    switch (check_state_) {
      case CHECK_STATE_REQUESTLINE: {
        // 解析请求行，并在最后更新状态机的下一个状态(即HEADER)
        // 并更新当前HTTP连接的成员url, ip, version等信息
        ret = ParseRequestLine(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        } else {
          break;  // 跳出switch语句
        }
      }
      case CHECK_STATE_HEADER: {
        ret = ParseHeader(text);       // 解析请求头
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        } else if (ret == GET_REQUEST) {
          // 在请求行和请求头都解析成功之后就可以进行响应了(HTTP请求报文可以没有请求实体)
          return DoRequest();  // 进行响应
        } else {
          break;
        }
      }
      case CHECK_STATE_CONTENT: {
        TODO:
        ret = ParseContent(text);
        if (ret == GET_REQUEST) {
          return DoRequest();
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
  return ret;
}

bool HttpConn::ProcessWrite(HTTP_CODE ret) {
  switch (ret) {
    case INTERNAL_ERROR: {
      AddStatusLine(500, error_500_title);
      AddHeaders(strlen(error_500_form));  // 传入响应实体的长度作为content-length返回
      if (!AddContent(error_500_form)) {
        // HTTP响应报文(加上响应实体后)超出写缓冲区的大小
        return false;
      }
      break;
    }
    case BAD_REQUEST: {
      AddStatusLine(400, error_400_title);
      AddHeaders(strlen(error_400_form));
      if (!AddContent(error_400_form)) {
        return false;
      }
      break;
    }
    case NO_RESOURCE: {
      AddStatusLine(400, error_404_title);
      AddHeaders(strlen(error_404_form));
      if (!AddContent(error_404_form)) {
        return false;
      }
      break;
    }
    case FORBIDDEN_REQUEST: {
      AddStatusLine(400, error_403_title);
      AddHeaders(strlen(error_403_form));
      if (!AddContent(error_403_form)) {
        return false;
      }
      break;
    }
    case FILE_REQUEST: {
      AddStatusLine(200, ok_200_title);
      if (file_stat_.st_size != 0) {  // 资源文件中有相应的内容
        AddHeaders(file_stat_.st_size);  // 传入的HTTP响应体长度，包括HTML文件的大小(以字节为单位)
        // 设置要分散写入的内存的起始地址和长度，以及写入块的数量
        // 准备将资源文件(响应实体)和写缓冲区中的内容(响应行和响应报文)写入连接fd中
        iv_[0].iov_base = write_buf_;
        iv_[0].iov_len = write_idx_;
        iv_[1].iov_base = file_address_;
        iv_[1].iov_len = file_stat_.st_size;
        iv_count_ = 2;
        return true;
      } else {
        // 资源文件存在但没有内容
        const char* ok_string = "<html><body></body></html>";
        AddHeaders(strlen(ok_string)); 
        if (!AddContent(ok_string)) {
          return false;
        }
      }
    }
    default: {
      return false;
    }
  }
  // 若没有获取资源文件，只需要将写缓冲区的内容(即响应行和响应头部)写到连接fd中即可
  iv_[0].iov_base = write_buf_;
  iv_[0].iov_len = write_idx_;
  iv_count_ = 1;
  return true;
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
  // 遇到空行，表示头部字段解析完毕
  if (text[0] == '\0') {  // 这里'\0'是在parseline中插入的
    // 若HTTP还有消息体，则需要将状态机转移到CONTENT状态
    if (content_length_ != 0) {
      check_state_ = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    } else {
      // 否则说明我们已经得到了一个完整的HTTP请求
      return GET_REQUEST;
    }
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    text += 5;
    host_ = strchr(text, ' ');
    if (!host_) {
      return BAD_REQUEST;
    }
    host_++;
    port_ = strpbrk(host_, ":");
    if (!port_) {
      return BAD_REQUEST;
    }
    *port_++ = '\0';
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    text += 11;
    // 跳过第一个空字符或者"\t"
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) {
      is_linger_ = true;
    }
  } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
    text += 15;
    // 跳过第一个空字符或者"\t"
    text += strspn(text, " \t");
    content_length_ = atol(text);
  } else {
#if LOG
    printf("未考虑解析的头部: %s\n", text);
#endif
  }
  return NO_REQUEST;
}

// 只是判断它是否被完整地读入了，并没有真正的解析
HttpConn::HTTP_CODE HttpConn::ParseContent(char *text)
{
  // content_length_为请求实体的长度
  if (read_idx_ >= (content_length_ + checked_idx_)) {  // 说明有请求实体
    text[content_length_] = '\0';
    return GET_REQUEST;
  } else {
    return NO_REQUEST;
  }
}

// 解析一行，判断条件为/r/n
HttpConn::LINE_STATUS HttpConn::ParseLine()
{
  char temp;
  // 一行一行解析读缓冲中的数据
  for (;checked_idx_ < read_idx_; ++checked_idx_) {
    temp = read_buf_[checked_idx_];  // 获取当前字符
    if (temp == '\r') {
      if (checked_idx_ + 1 == read_idx_) {
        // 若当前行最后一个字符为\r且下一行没有数据(即没有\n)时，返回数据不完整状态
        return LINE_OPEN;
      } else if (read_buf_[checked_idx_+1] == '\n') {
        // 读到HTTP一行的结束符\r\n，并替换成字符串结束符方便提取内容，返回数据完整信息
        read_buf_[checked_idx_++] = '\0';
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      } else {
        // 否则返回行出错状态
        return LINE_BAD;
      }
    } else if (temp == '\n') {
      // 这种情况保证，若上一次读到的最后一个字符为'\r'但并没有读完，这一次也得进行判断
      // checked_idx_ > 1保证数组访问不溢出
      if (checked_idx_ > 1 && read_buf_[checked_idx_-1] == '\r') {
        read_buf_[checked_idx_++] = '\0';  // 换位字符串结束符，读取字符串
        read_buf_[checked_idx_++] = '\0';
        return LINE_OK;
      } else {
        return LINE_BAD;
      }
    }
  }
  // 遍历的字符中未出现\r或者\n，说明行数据不完整
  return LINE_OPEN;
}

HttpConn::HTTP_CODE HttpConn::DoRequest()
{
  // "/home/moksha/webserver/resources"  服务器资源
  strcpy(real_file_, doc_root);
  int len = strlen(doc_root);
  // 连接doc_root + url_获得完整路径
  strncpy(real_file_ + len, url_, FILENAME_LEN - len - 1);  // 这里的len为预留给doc_root的长度，1为字符串结束符
  // stat通过文件路径名获取元数据，返回值-1为error，0为成功
  if (stat(real_file_, &file_stat_) < 0) {
    // 获取不到元数据
    return NO_RESOURCE;
  } else if  (!(file_stat_.st_mode & S_IROTH)) {
    // 没有访问权限
    return FORBIDDEN_REQUEST;
  } else if (S_ISDIR(file_stat_.st_mode)) {
    // 请求的资源得是一个文件而不是目录
    return BAD_REQUEST;
  }
  // 以只读权限打开资源
  int fd = open(real_file_, O_RDONLY);
  // PORT_READ描述该映射区域的保护权限(Protection)
  // mmap成功时将返回指向该映射区域的指针
  // 将资源文件映射到内存中。第一个参数为NULL内核会自动选择一个地址进行映射
  // MAP_PRIVATE表示更新这段区域对其他进程是不可见的
  file_address_ = mmap(NULL, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);  // mmap成功返回后要用close关闭fd
  return FILE_REQUEST;  // 返回文件请求成功状态
}

void HttpConn::unmap() {
  if (file_address_ != (void*)(-1)) {  // mmap失败时返回-1
    munmap(file_address_, file_stat_.st_size);
    file_address_ = 0;
  }
}

// 可变参数函数，format为printf所需要的格式符
bool HttpConn::AddResponse(const char* format, ...) {
  if (write_idx_ >= WRITE_BUFFER_SIZE) {   
    // 若写入写缓冲区的数据大于写缓冲的大小
    return false;
  }
  // 需要用到以下宏支持可变参数
  va_list arg_list;
  // va_start第二个参数为可变参数之前的参数。初始化可变参数列表，将arg_list指向第一个可变参数
  va_start(arg_list, format);

  // 将可变参数(响应报文的字符串信息)打印到write_buf_中, 成功返回打印的字符个数
  // 传入的参数都为字符串类型
  int len = vsnprintf(write_buf_ + write_idx_, WRITE_BUFFER_SIZE - 1 - write_idx_, format, arg_list);
  if (len >= (WRITE_BUFFER_SIZE - 1 - write_idx_)) {  // 若超出了写缓冲区的大小
    return false;
  }
  write_idx_ += len;  // 更新已写入缓冲区的字节数(char刚好为1字节)
  va_end(arg_list);   // 清理va_list变量的内存，将arg_list置为NULL
  return true;
}

bool HttpConn::AddStatusLine(int status, const char* title) {
  return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void HttpConn::AddHeaders(int content_length) {
  AddContentLength(content_length);
  AddLinger();
  AddContentType();
  AddBlankLine();
}

bool HttpConn::AddContentLength(int content_length) {
  return AddResponse("Content-Length: %d\r\n", content_length);
}

bool HttpConn::AddLinger() {
  return AddResponse("Connection: %s\r\n", is_linger_ == true? "keep-alive": "close");
}

bool HttpConn::AddBlankLine() {
  return AddResponse("%s", "\r\n");
}

bool HttpConn::AddContent(const char* content) {
  return AddResponse("%s", content);
}

bool HttpConn::AddContentType() {
  return AddResponse("Content-Type:%s\r\n", "text/html");
}
