#include "locker.h"

bool Locker::Lock() {
  return pthread_mutex_lock(&mutex_) == 0;
}

bool Locker::UnLock() {
  return pthread_mutex_unlock(&mutex_) == 0;
}

pthread_mutex_t* Locker::Get() {
  return &mutex_;
}

bool Cond::Wait(pthread_mutex_t* mutex) {
  return pthread_cond_wait(&cond_, mutex) == 0;
}

bool Cond::Signal() {
  return pthread_cond_signal(&cond_);
}
 
bool Cond::TimeWait(pthread_mutex_t* mutex, timespec* t) {
  return pthread_cond_timedwait(&cond_, mutex, t) == 0;
}

bool Cond::Broadcast() {
  return pthread_cond_broadcast(&cond_) == 0;
}

bool Sem::Wait() {
  return sem_wait(&sem_) == 0;
}

bool Sem::Post() {
  return sem_post(&sem_) == 0;
}