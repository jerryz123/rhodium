/* Runs fixed software-NoC partitions with one persistent pthread per worker and release/acquire joins. */
#ifndef SOC_NOC_SOFTWARE_RUNTIME_H
#define SOC_NOC_SOFTWARE_RUNTIME_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <immintrin.h>
typedef void(*snoc_work)(void*);
typedef struct{_Alignas(64) _Atomic uint64_t epoch;}snoc_epoch;
struct snoc_pool;
typedef struct{struct snoc_pool*pool;unsigned id;}snoc_worker;
typedef struct snoc_pool{
 snoc_epoch command,done[8];pthread_t threads[7];snoc_worker worker[8];
 void*context;const snoc_work*work;unsigned count,created;bool stop;
}snoc_pool;
static void*snoc_worker_main(void*argument){snoc_worker*w=argument;snoc_pool*p=w->pool;uint64_t seen=0;for(;;){uint64_t current;while((current=atomic_load_explicit(&p->command.epoch,memory_order_acquire))==seen)_mm_pause();if(p->stop)return NULL;
#ifdef RDS_NOC_SOFTWARE_DELAY
 if(((current+w->id*17)&127)==0)for(unsigned delay=0;delay<2048;++delay)_mm_pause();
#endif
 p->work[w->id](p->context);seen=current;atomic_store_explicit(&p->done[w->id].epoch,seen,memory_order_release);}}
static void snoc_pool_destroy(snoc_pool*p){p->stop=true;atomic_fetch_add_explicit(&p->command.epoch,1,memory_order_release);for(unsigned i=0;i<p->created;++i)pthread_join(p->threads[i],NULL);}
static bool snoc_pool_init(snoc_pool*p,void*context,const snoc_work*work,unsigned count){p->context=context;p->work=work;p->count=count;atomic_init(&p->command.epoch,0);for(unsigned i=0;i<count;++i)atomic_init(&p->done[i].epoch,0);for(unsigned i=1;i<count;++i){p->worker[i]=(snoc_worker){p,i};if(pthread_create(&p->threads[i-1],NULL,snoc_worker_main,&p->worker[i])){snoc_pool_destroy(p);return false;}++p->created;}return true;}
static void snoc_pool_run(snoc_pool*p){uint64_t epoch=atomic_fetch_add_explicit(&p->command.epoch,1,memory_order_release)+1;p->work[0](p->context);for(unsigned i=1;i<p->count;++i)while(atomic_load_explicit(&p->done[i].epoch,memory_order_acquire)!=epoch)_mm_pause();}
#endif
