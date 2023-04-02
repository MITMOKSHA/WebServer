#ifndef LOCKER_H_
#define LOCKER_H_

#include <pthread.h>
#include <semaphore.h>
#include <exception>

class Locker {
public:
  Locker() {
    if (pthread_mutex_init(&mutex_, nullptr) != 0) {
      throw std::exception();
    }
  }
  ~Locker() {
    pthread_mutex_destroy(&mutex_);
  }
  bool Lock();
  bool UnLock();
  pthread_mutex_t* Get();
private:
  pthread_mutex_t mutex_;
};

class Cond {
  Cond() {
    if (pthread_cond_init(&cond_, NULL) != 0)
      throw std::exception();
  }
  ~Cond() {
    pthread_cond_destroy(&cond_);
  }
  bool Wait(pthread_mutex_t*);
  bool TimeWait(pthread_mutex_t*, timespec*);
  bool Signal();
  bool Broadcast();
private:
  pthread_cond_t cond_;
};

class Sem {
public:
  Sem() {
    if (sem_init(&sem_, 0, 0) != 0)
      throw std::exception();
  }
  Sem(int num) {
    if (sem_init(&sem_, 0, num) != 0)
      throw std::exception();
  }
  ~Sem() {
    sem_destroy(&sem_);
  }
  bool Wait();
  bool Post();
private:
  sem_t sem_;
};
#endif