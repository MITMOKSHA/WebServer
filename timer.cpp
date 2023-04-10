#include "timer.h"

void SortTimerList::AddTimer(Timer* timer) {
  // 该定时器未初始化
  if (!timer) {
    return;
  }
  // 若该定时器链表中没有定时器
  if (!head_) {
    head_ = tail_ = timer;
    return;
  }
  // 直接插入头部的
  if (timer->expire_ < head_->expire_) {
    timer->next_ = head_;
    head_->prev_ = timer;
    // 更新定时器链表的头节点
    head_ = timer;
    return;
  }
  // 调用重载函数插入合适的位置
  AddTimer(timer, head_);
}

void SortTimerList::AdjustTimer(Timer* timer) {
  if (!timer) {
    return;
  }
  Timer* node = timer->next_;
  // 若被调整的定时器已经在尾部(到期绝对时间最长)或者它在合适的位置上则不需调整
  if (!node || timer->expire_ < node->expire_) {
    return;
  }
  if (timer == head_) {
    // 将其从该位置删除
    head_ = head_->next_;
    head_->prev_ = nullptr;
    timer->next_ = nullptr;
    // 再添加到合适的位置
    AddTimer(timer, head_);
  } else {
    // 如果目标定时器不是链表的头节点(当然前面的case也说明了尾节点不需要调整)
    // 则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
    timer->prev_->next_ = timer->next_;
    timer->next_->prev_ = timer->prev_;
    AddTimer(timer, timer->next_);
  }
}

void SortTimerList::DelTimer(Timer* timer) {
  if (!timer) {
    return;
  }
  // 下面这个条件成立表示链表中只有一个定时器，即目标定时器
  if (timer == head_ && timer == tail_) {
    delete timer;
    head_ = nullptr;
    tail_ = nullptr;
    return;
  }
  // 如果链表中至少有两个定时器
  // 该定时器是头节点
  if (timer == head_) {
    head_ = head_->next_;
    head_->prev_ = nullptr;
    delete timer;
    return;
  }
  // 该定时器是尾节点
  if (timer == tail_) {
    tail_ = tail_->prev_;
    tail_->next_ = nullptr;
    delete timer;
    return;
  }
  // common case位于定时器链表的中间
  timer->prev_->next_ = timer->next_;
  timer->next_->prev_ = timer->prev_;
  delete timer;
}

void SortTimerList::Tick() {
  if (!head_) {
    return;
  }
  printf("Time tick!\n");
  // 绝对时间
  time_t cur_abtime = time(NULL);  // 获取当前的系统时间
  Timer* cur = head_;
  // 从头节点开始一次处理每个定时器，直到遇到一个尚未到期的定时器
  while (cur) {
    if (cur_abtime < cur->expire_) {
      // 直到链表中不存在到期的定时器，跳出循环
      break;
    }
    // 直到找到第一个超时的任务
    // 超时调用回调函数
    cur->cb_func_(cur->sockfd_);
    // 执行完定时器中的任务后，就将它从定时器链表中删除(因为超时了)
    head_ = cur->next_;
    if (head_) {
      head_->prev_ = nullptr;
    }
    delete cur;
    cur = head_;
  }
}

// 第二个参数为遍历起点的timer
void SortTimerList::AddTimer(Timer *timer, Timer *start) {
  Timer* prev = start;
  Timer* cur = prev->next_;
  // 遍历list_head节点之后的部分链表，直到找到一个超时时间大于目标定时器
  // 的超时时间节点
  while (cur) {
    if (timer->expire_ < cur->expire_) {
      prev->next_ = timer;
      timer->next_ = cur;
      cur->prev_ = timer;
      timer->prev_ = prev;
      break;
    }
    prev = cur;
    cur = cur->next_;
  }
  // 如果遍历完定时器链表仍未找到合适的位置，只能在链表的尾部插入该定时器，并更新成员变量
  if (!cur) {
    prev->next_ = timer;
    timer->prev_ = prev;
    timer->next_ = nullptr;
    // 更新尾部元素bookkeeping
    tail_ = timer;
  }
}
