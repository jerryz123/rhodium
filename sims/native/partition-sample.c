/* Samples Linux/x86-64 simulator threads with isolated CPU-time instruction buffers. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
typedef struct {timer_t timer;unsigned count,lost;pid_t tid;uintptr_t ip[65536];} samples;
static _Thread_local samples *local;
static _Atomic int sample_enabled=1;
static long sample_ns=1009000;
void rds_partition_sample_window(int enabled){atomic_store_explicit(&sample_enabled,enabled!=0,memory_order_relaxed);}
static void capture(int sig,siginfo_t *info,void *context){(void)sig;if(!atomic_load_explicit(&sample_enabled,memory_order_relaxed))return;samples*s=info->si_value.sival_ptr;if(s->count<65536)s->ip[s->count++]=((ucontext_t*)context)->uc_mcontext.gregs[REG_RIP];else ++s->lost;}
static void start_thread(void){if(!getenv("RDS_PARTITION_SAMPLE"))return;local=calloc(1,sizeof *local);if(!local)abort();local->tid=(pid_t)syscall(SYS_gettid);
 struct sigevent event={0};event.sigev_notify=SIGEV_THREAD_ID;event.sigev_signo=SIGRTMIN+5;event.sigev_value.sival_ptr=local;event._sigev_un._tid=local->tid;
 if(timer_create(CLOCK_THREAD_CPUTIME_ID,&event,&local->timer))abort();struct itimerspec interval={{0,sample_ns},{0,sample_ns}};if(timer_settime(local->timer,0,&interval,NULL))abort();}
static void finish_thread(void){if(!local)return;sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGRTMIN+5);pthread_sigmask(SIG_BLOCK,&signals,NULL);timer_delete(local->timer);
 char path[4096];snprintf(path,sizeof path,"%s.%d.samples",getenv("RDS_PARTITION_SAMPLE"),local->tid);FILE*f=fopen(path,"w");if(!f)abort();fprintf(f,"# samples=%u lost=%u\n",local->count,local->lost);for(unsigned i=0;i<local->count;++i)fprintf(f,"%lx\n",(unsigned long)local->ip[i]);fclose(f);
 /* Keep the buffer alive until the thread exits: a pending signal still names it. */
 local=NULL;
}
typedef struct {void *(*entry)(void*);void *argument;} launch;
static void *thread_main(void *p){launch l=*(launch*)p;free(p);start_thread();void*result=l.entry(l.argument);finish_thread();return result;}
int pthread_create(pthread_t*t,const pthread_attr_t*a,void*(*entry)(void*),void*argument){int(*next)(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*)=dlsym(RTLD_NEXT,"pthread_create");if(!getenv("RDS_PARTITION_SAMPLE"))return next(t,a,entry,argument);launch*l=malloc(sizeof *l);if(!l)abort();*l=(launch){entry,argument};int rc=next(t,a,thread_main,l);if(rc)free(l);return rc;}
static void record_maps(void){const char*prefix=getenv("RDS_PARTITION_SAMPLE");if(prefix){char path[4096],line[4096];snprintf(path,sizeof path,"%s.maps",prefix);FILE*in=fopen("/proc/self/maps","r"),*out=fopen(path,"w");if(!in||!out)abort();while(fgets(line,sizeof line,in))fputs(line,out);fclose(in);fclose(out);}}
int dlclose(void*h){record_maps();int(*next)(void*)=dlsym(RTLD_NEXT,"dlclose");return next(h);}
__attribute__((constructor))static void begin(void){if(!getenv("RDS_PARTITION_SAMPLE"))return;if(getenv("RDS_PARTITION_WINDOWED"))rds_partition_sample_window(0);const char*period=getenv("RDS_PARTITION_SAMPLE_NS");if(period){char*end;sample_ns=strtol(period,&end,10);if(*end||sample_ns<100000||sample_ns>10000000)abort();}struct sigaction action={0};action.sa_sigaction=capture;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);if(sigaction(SIGRTMIN+5,&action,NULL))abort();record_maps();start_thread();}
__attribute__((destructor))static void end(void){finish_thread();}
