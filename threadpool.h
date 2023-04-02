#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"

template <class T>
class ThreadPool {
public:
  ThreadPool(int thread_number = 8, int max_requests = 10000);
  ~ThreadPool();
  bool Append(T* request);      // 往请求队列中添加任务
  // 工作线程运行的函数，它不断从工作队列中取出任务并执行
  static void* Worker(void* args);   
  void Run();                   
private:
  int thread_numbers_;        // 线程的数量
  pthread_t* threads_;        // 线程池中的线程数组
  int max_requests_;          // 请求队列的容量
  std::list<T*> workqueue_;   // 请求队列
  Locker queuelock_;          // 请求队列锁
  Sem queuestat_;             // 信号量用来判断是否请求队列中有任务需要处理
  bool is_stop_;              // 是否结束线程
};

template <class T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) : 
  thread_numbers_(thread_number), max_requests_(max_requests), is_stop_(false), 
  threads_(NULL) {
  if (thread_number <= 0 || max_requests <= 0) {
    throw std::exception();
  }
  // 初始化线程池中的线程数组
  threads_ = new pthread_t[thread_number];
  for (int i = 0; i < thread_number; ++i) {
    printf("create the %dth thread\n", i);
    if (pthread_create(threads_+i, NULL, Worker, this) != 0) {  // 创建对应线程池中的线程
      delete[] threads_;
      throw std::exception();
    }
    if (pthread_detach(threads_[i]) != 0) {  // 分离线程
      delete[] threads_;
      throw std::exception();
    }
  }
}

template <class T>
ThreadPool<T>::~ThreadPool() {
  delete[] threads_;
  is_stop_ = true;        // 线程池停止
}

template <class T>
bool ThreadPool<T>::Append(T* requests) { 
  if (requests == nullptr) {
    return false;
  }
  queuelock_.Lock();
  if (workqueue_.size() >= max_requests_) {
    return false;
  }
  workqueue_.push_back(requests);
  queuelock_.UnLock();
  queuestat_.Post();                      // 唤醒等待的工作线程处理任务
  return true;
}

template <class T>
void* ThreadPool<T>::Worker(void* args) {
  ThreadPool* pool = (ThreadPool*)args;  // 传入的this指针
  pool->Run();                           // 运行线程池处理任务
  return pool;
}

template <class T>
void ThreadPool<T>::Run() {
  while (!is_stop_) {
    queuestat_.Wait();                    // 工作线程休眠等待有任务唤醒
    queuelock_.Lock();
    if (workqueue_.empty()) {
      queuelock_.UnLock();
      continue;
    }
    T* request = workqueue_.front();      // 从工作队列中取任务(函数)
    workqueue_.pop_front();               
    queuelock_.UnLock();
    request->Process();                   // 工作线程处理任务
  }
}
#endif