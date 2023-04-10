#ifndef TIMER_H_
#define TIMER_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>

// class Timer;

// struct ClientData {
//   sockaddr_in addr_;   // 客户socket地址
//   int sockfd_;         // 客户的连接fd
//   Timer *timer_;    // 定时器
// };

// 定时器
class Timer {
public:
  Timer() : prev_(nullptr), next_(nullptr) {}
  ~Timer() {}
  void (*cb_func_)(int);  // 任务回调函数
  time_t expire_;          // 超时时间
  int sockfd_;          // 客户连接的fd
  // ClientData* user_data_;
  // 双向链表
  Timer* prev_;        // 上一个指针
  Timer* next_;        // 下一个指针
};

// 定时器升序链表(按超时绝对时间进行排序)
class SortTimerList {
public:
  SortTimerList() : head_(nullptr), tail_(nullptr) {}
  ~SortTimerList() {
    // 释放掉定时器升序链表的对象
    Timer* cur = head_;
    while (cur) {
      head_ = cur->next_;
      delete cur;
      cur = head_;
    }
  }

  void AddTimer(Timer* timer);    // 将定时器加入定时器链表中
  void AdjustTimer(Timer* timer); // 调整定时器在定时器链表中的位置
  void DelTimer(Timer* timer);    // 从定时器链表中删除定时器
  // 处理定时器链表上到期的任务
  void Tick();                    // SIGALARM信号每次触发就在信号处理函数中执行一次Tick()函数
private:
  void AddTimer(Timer* timer, Timer* start);
  Timer* head_;
  Timer* tail_;
};

#endif