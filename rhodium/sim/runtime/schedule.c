/* Builds static disjoint cones, reuses dead scratch, and runs persistent C workers. */
#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <pthread.h>
#include <sched.h>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "schedule.h"
enum { COMPLETION_FAILED = 1, PREPARATION_FAILED = 2, EPOCH_STEP = 4 };

static void pause_worker(uint32_t *spins) {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    atomic_signal_fence(memory_order_seq_cst);
#endif
    if (++*spins == 4096) { sched_yield(); *spins = 0; }
}

/* Each arrival has one writer. Worker zero acquires arrivals before publishing
 * one shared release, avoiding all-to-all cache-line readership. Workers stay
 * in the cycle loop rather than returning to the host for each phase. */
static void bulk_barrier(worker *w, uint64_t phase) {
    rds_schedule *p=w->plan;
    if(p->count==2){
        atomic_store_explicit(&p->bulk_progress[w->id].epoch,phase,memory_order_release);
        uint32_t spins=0;
        while(atomic_load_explicit(&p->bulk_progress[1-w->id].epoch,memory_order_acquire)<phase)pause_worker(&spins);
        return;
    }
    if(w->id){
        atomic_store_explicit(&p->bulk_progress[w->id].epoch,phase,memory_order_release);
        uint32_t spins=0;
        while(atomic_load_explicit(&p->bulk_progress[0].epoch,memory_order_acquire)<phase)pause_worker(&spins);
    }else{
        for(uint32_t peer=1;peer<p->count;++peer){uint32_t spins=0;
            while(atomic_load_explicit(&p->bulk_progress[peer].epoch,memory_order_acquire)<phase)pause_worker(&spins);
        }
        atomic_store_explicit(&p->bulk_progress[0].epoch,phase,memory_order_release);
    }
}
static void bulk_failure(worker *w,uint64_t phase){
    /* Ignore a future phase's failure until this worker reaches that barrier. */
    w->status=-1;
    uint64_t old=atomic_load_explicit(&w->plan->bulk_failure_phase,memory_order_relaxed);
    while(old>phase&&!atomic_compare_exchange_weak_explicit(&w->plan->bulk_failure_phase,
        &old,phase,memory_order_release,memory_order_relaxed)){}
}
static int run_cycles(worker *w) {
    rds_schedule *p=w->plan;const rds_sim *s=p->sim;
    rds_compiled_context c={s->arena,s->current,s->next,s->objects,
        p->compiled_memories,w->view.error,s->cycles,s->strict,p->compiled_hot};
    const rds_phase_fn *entry=p->compiled_entries+3*w->id;
    const rds_phase_fn *effects=p->compiled_entries+3*p->count;
    uint64_t phase=0,completed=0;
    bool fused=p->compiled_bulk_fused&&!s->strict;
    rds_phase_fn transition=fused?p->compiled_entries[3*p->count+4+w->id]:NULL;
    w->status=0;w->view.error[0]=0;
    const rds_phase_fn *offers=p->compiled_entries+4*p->count+4+2*w->id;
    if(fused&&offers[0]&&offers[1]){
        /* Initialization establishes the current immutable offers. Each step
         * reads that bank, mutates only owned state, and fills the other bank.
         * The sole round protects old readers and publishes next offers/FFs. */
        if(!p->offers_ready){
            if(offers[0](&c))bulk_failure(w,phase+1);
            bulk_barrier(w,++phase);
        }
        if(atomic_load_explicit(&p->bulk_failure_phase,memory_order_acquire)>phase){
            for(;completed<p->bulk_cycles;++completed){
                offers[1](&c);
                /* The caller acquires every worker completion before exposing
                 * the final state or launching another batch. Only edges with
                 * a following in-batch reader need a separate rendezvous. */
                if(completed+1<p->bulk_cycles)bulk_barrier(w,++phase);
                uint64_t *old=c.q;c.q=c.next;c.next=old;++c.cycle;
            }
        }
        if(!w->id){p->compiled_entries[6*p->count+4](&c);p->bulk_completed=completed;}
        return w->status;
    }
    for(;completed<p->bulk_cycles;++completed){
        int status=entry[0](&c);
        if(p->bulk_local&&!transition&&!status)status=entry[1](&c);
        if(status)bulk_failure(w,phase+1);
        bulk_barrier(w,++phase);
        if(atomic_load_explicit(&p->bulk_failure_phase,memory_order_acquire)<=phase)break;
        if(transition){
            if(!w->id){effects[0](&c);effects[1](&c);}
            transition(&c);
        }else{
            if(!p->bulk_local){
                status=entry[1](&c);
                if(!w->id){int validation=effects[0](&c);if(validation)status=validation;}
                if(status)bulk_failure(w,phase+1);
                /* The generated release capability proves these preparations
                 * cannot fail and read only owned state or pinned old inputs.
                 * Publication can overlap another owner's preparation. */
                if(!fused){
                    bulk_barrier(w,++phase);
                    if(atomic_load_explicit(&p->bulk_failure_phase,memory_order_acquire)<=phase)break;
                }
            }
            /* Every validation and snapshot read has finished. Memory writes read
             * pinned arena/current-bank sources; object proposals are already owned
             * copies. These disjoint publications may proceed concurrently. */
            if(!w->id){if(p->bulk_local)effects[0](&c);effects[1](&c);}
            entry[2](&c);
        }
        bulk_barrier(w,++phase);
        uint64_t *old=c.q;c.q=c.next;c.next=old;++c.cycle;
    }
    if(!w->id)p->bulk_completed=completed;
    return w->status;
}

static int run_instructions(void *context, uint32_t first, uint32_t count) {
    worker *w = context;
    rds_schedule *p = w->plan;
    rds_sim *s = &w->view;
    uint64_t *v = s->arena;
    uint32_t low = w->first, high = w->first + w->count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        batch *b = &p->batches[mid];
        if (b->first + b->count <= first) low = mid + 1; else high = mid;
    }
    for (uint32_t k = low; k < w->first + w->count; ++k) {
        batch *b = &p->batches[k];
        if (b->first >= first + count) break;
        uint32_t begin_id = b->first > first ? b->first : first;
        uint32_t end_id = b->first + b->count < first + count ? b->first + b->count : first + count;
        instruction *begin = p->instructions + begin_id, *end = p->instructions + end_id;
        uint64_t mask = rds_mask(b->width);
        /* Dispatch once per homogeneous batch, with sequential descriptor reads. */
#define LOOP(expr) for (instruction *i = begin; i != end; ++i) v[i->out] = (expr) & mask
        switch (b->code) {
        case RDS_COPY: case RDS_ZEXT: LOOP(v[i->a]); break;
        case RDS_NOT: LOOP(~v[i->a]); break;
        case RDS_AND: LOOP(v[i->a] & v[i->b]); break;
        case RDS_OR: LOOP(v[i->a] | v[i->b]); break;
        case RDS_XOR: LOOP(v[i->a] ^ v[i->b]); break;
        case RDS_ADD: LOOP(v[i->a] + v[i->b]); break;
        case RDS_SUB: LOOP(v[i->a] - v[i->b]); break;
        case RDS_MUL: LOOP(v[i->a] * v[i->b]); break;
        case RDS_SET_CLEAR: LOOP((v[i->a] | v[i->b]) & ~v[i->c]); break;
        case RDS_BALANCE: LOOP(v[i->a] + v[i->b] - v[i->c]); break;
        case RDS_COUNTER_STEP: LOOP(!v[i->b] ? v[i->a] : v[i->a] == v[i->c] ? 0 : v[i->a] + 1); break;
        case RDS_EQ: LOOP(v[i->a] == v[i->b]); break;
        case RDS_ULT: LOOP(v[i->a] < v[i->b]); break;
        case RDS_SHL: LOOP(v[i->b] < b->width ? v[i->a] << v[i->b] : 0); break;
        case RDS_SHRU: LOOP(v[i->b] < b->width ? v[i->a] >> v[i->b] : 0); break;
        case RDS_SLICE: LOOP(v[i->a] >> i->c); break;
        case SLICE_CROSS: LOOP((v[i->a] >> i->c) | (v[i->a + 1] << (64 - i->c))); break;
        case SELECT: LOOP(v[i->a] ? v[i->c] : v[i->b]); break;
        default:
            for (instruction *i = begin; i != end; ++i)
                if (rds_execute(s, &s->ops[i->out])) return -1;
        }
#undef LOOP
    }
    return 0;
}
static int run(worker *w) {
    rds_schedule *p = w->plan;
    if(p->bulk_cycles)return run_cycles(w);
    rds_sim *s = &w->view;
    s->error[0] = 0;
    if (p->compiled_entries) {
        const rds_sim *main = p->sim;
        rds_compiled_context c={main->arena,main->current,main->next,main->objects,
            p->compiled_memories,s->error,main->cycles,main->strict,p->compiled_hot};
        const rds_phase_fn *entry=p->compiled_entries+3*w->id;
        if (p->phase) return entry[p->phase](&c);
        if (entry[0](&c)) return -1;
        if (p->prepare_local) {
            w->prepared = entry[1](&c);
            if (w->prepared) memcpy(w->prepare_error, s->error, sizeof w->prepare_error);
            s->error[0] = 0;
        }
        return 0;
    }
    s->strict = p->sim->strict;
    s->arena = p->sim->arena;
    s->current = p->sim->current; s->next = p->sim->next; s->cycles = p->sim->cycles;
    if (p->phase==1) return rds_objects_prepare_range(s,w->id,p->count);
    if (p->phase==2) {
        if (p->prepare_local) for (uint32_t i = 0; i < w->object_count; ++i) rds_object_commit(s, w->objects[i]);
        else rds_objects_commit_range(s,w->id,p->count);
        return 0;
    }
    if (w->count) {
        uint32_t first = p->batches[w->first].first;
        batch *last = &p->batches[w->first + w->count - 1];
        if (run_instructions(w, first, last->first + last->count - first)) return -1;
    }
    if (p->prepare_local) {
        w->prepared = 0;
        for (uint32_t i = 0; i < w->object_count; ++i) if (rds_object_prepare(s, w->objects[i])) {
            w->prepared = -1; memcpy(w->prepare_error, s->error, sizeof w->prepare_error); break;
        }
        /* Evaluation remains pure and preparation failures are edge failures.
         * No validation failure or host effect becomes observable before advance. */
        s->error[0] = 0;
    }
    return 0;
}
static void *worker_main(void *arg) {
    worker *w = arg;
    rds_schedule *p = w->plan;
    pthread_mutex_lock(&p->gate);
    while (!p->ready) pthread_cond_wait(&p->gate_changed, &p->gate);
    int ready = p->ready;
    pthread_mutex_unlock(&p->gate);
    if (ready < 0) return NULL;
    if (p->spin) {
        uint64_t seen = 0;
        for (;;) {
            uint64_t current; uint32_t spins = 0;
            while ((current = atomic_load_explicit(&p->epoch, memory_order_acquire)) == seen) pause_worker(&spins);
            if (p->stop) break;
            w->status = run(w); seen = current;
            uint64_t status=(w->status?COMPLETION_FAILED:0)|(w->prepared?PREPARATION_FAILED:0);
            atomic_store_explicit(&p->completions[w->id].epoch, seen|status, memory_order_release);
        }
        return NULL;
    }
    for (;;) {
        pthread_barrier_wait(&p->start);
        if (p->stop) break;
        w->status = run(w);
        pthread_barrier_wait(&p->done);
    }
    return NULL;
}
void rds_schedule_free(rds_sim *s) {
    rds_schedule *p = s->schedule;
    if (!p) return;
    if (p->synchronization) {
        if (p->ready > 0) {
            p->stop = true;
            if (p->spin) atomic_fetch_add_explicit(&p->epoch, EPOCH_STEP, memory_order_release);
            else pthread_barrier_wait(&p->start);
        } else {
            pthread_mutex_lock(&p->gate);
            p->ready = -1;
            pthread_cond_broadcast(&p->gate_changed);
            pthread_mutex_unlock(&p->gate);
        }
        for (uint32_t i = 0; i < p->created; ++i) pthread_join(p->workers[i + 1].thread, NULL);
        pthread_barrier_destroy(&p->start); pthread_barrier_destroy(&p->done);
        pthread_cond_destroy(&p->gate_changed); pthread_mutex_destroy(&p->gate);
    }
    if (p->workers) for (uint32_t i = 0; i < p->count; ++i) free(p->workers[i].objects);
    rds_compiled_free(s);
    rds_offer_free(p->offers);
    free(p->constants); free(p->completions); free(p->bulk_progress); free(p->workers); free(p->batches); free(p->instructions); free(p->instruction_origins); free(p->instruction_guards); free(p->guard_unions); free(p->instruction_cache); free(p->cache_objects); free(p->cache_lanes); free(p->cache_fields); free(p->field_masks); free(p->flow_cache_objects); free(p);
    s->schedule = NULL;
}
int rds_schedule_eval(rds_sim *s) {
    rds_schedule *p = s->schedule;
    if (p->compiled_entries && p->count == 1) {
        rds_compiled_context c={s->arena,s->current,s->next,s->objects,
            p->compiled_memories,s->error,s->cycles,s->strict,p->compiled_hot};
        if (p->compiled_entries[0](&c)) return -1;
        worker *w = &p->workers[0];
        w->prepared = p->compiled_entries[1](&c);
        if (w->prepared) memcpy(w->prepare_error,s->error,sizeof w->prepare_error);
        s->error[0] = 0;
        return 0;
    }
    if (rds_schedule_objects(s,0)) return -1;
    if (p->tail_count) {
        if (p->compiled_entries) {
            rds_compiled_context c={s->arena,s->current,s->next,s->objects,
                p->compiled_memories,s->error,s->cycles,s->strict,p->compiled_hot};
            return p->compiled_entries[3*p->count+3](&c);
        }
        for (uint32_t i=0;i<p->tail_count;++i)
            if (rds_execute(s,&s->ops[p->instructions[p->tail_first+i].out])) return -1;
    }
    return 0;
}
int rds_schedule_objects(rds_sim *s, unsigned phase) {
    rds_schedule *p = s->schedule;
    /* Pure evaluation can refresh outputs without changing state-only offers. */
    if(!p->bulk_cycles&&phase!=0)p->offers_ready=false;
    if (phase == 1 && p->prepare_local) {
        for (uint32_t i = 0; i < p->count; ++i)
            if (p->spin && i ? (atomic_load_explicit(&p->completions[i].epoch,memory_order_relaxed)&PREPARATION_FAILED) != 0 : p->workers[i].prepared != 0)
            return rds_fail(s, p->workers[i].prepare_error);
        return 0;
    }
    p->phase=phase;
    uint64_t epoch = 0;
    if (p->count > 1) {
        if (p->spin) {
            /* The simulation caller is the sole command writer. Readers only
             * acquire its epoch, so publishing needs no read/modify/write. */
            epoch=atomic_load_explicit(&p->epoch,memory_order_relaxed)+EPOCH_STEP;
            atomic_store_explicit(&p->epoch,epoch,memory_order_release);
        }
        else pthread_barrier_wait(&p->start);
    }
    p->workers[0].status = run(&p->workers[0]);
    if (p->count > 1) {
        if (p->spin) for (uint32_t i = 1; i < p->count; ++i) {
            uint32_t spins = 0;
            while ((atomic_load_explicit(&p->completions[i].epoch, memory_order_acquire)&~UINT64_C(3)) != epoch) pause_worker(&spins);
        }
        else pthread_barrier_wait(&p->done);
    }
    for (uint32_t i = 0; i < p->count; ++i)
        if (p->spin && i ? (atomic_load_explicit(&p->completions[i].epoch,memory_order_relaxed)&COMPLETION_FAILED) != 0 : p->workers[i].status != 0)
            return rds_fail(s, p->workers[i].view.error);
    return 0;
}
int rds_advance_cycles(rds_sim *s,uint64_t cycles) {
    if(!cycles)return 0;
    if(s->evaluated){if(rds_advance(s))return -1;--cycles;}
    rds_schedule *p=s->schedule;
    if(!cycles)return 0;
    if(!p||!p->compiled_entries||p->count<2||!p->bulk_safe||s->copy_state||s->object_trace)
        {while(cycles--)if(rds_advance(s))return -1;return 0;}
    while(cycles){
        s->error[0]=0;
        uint64_t count=cycles>UINT64_MAX/4?UINT64_MAX/4:cycles;
        for(uint32_t i=0;i<p->count;++i)atomic_store_explicit(&p->bulk_progress[i].epoch,0,memory_order_relaxed);
        atomic_store_explicit(&p->bulk_failure_phase,UINT64_MAX,memory_order_relaxed);
        p->bulk_completed=0;p->bulk_cycles=count;
        int status=rds_schedule_objects(s,0);
        p->offers_ready=!status&&p->compiled_bulk_fused&&!s->strict&&p->compiled_entries[4*p->count+4]!=NULL;
        p->bulk_cycles=0;
        if(p->bulk_completed&1){uint64_t *old=s->current;s->current=s->next;s->next=old;}
        s->cycles+=p->bulk_completed;s->evaluated=false;
        if(status)return -1;
        cycles-=count;
    }
    return 0;
}
static uint32_t root(uint32_t *parent, uint32_t i) {
    uint32_t r = i;
    while (parent[r] != r) r = parent[r];
    while (parent[i] != i) { uint32_t n = parent[i]; parent[i] = r; i = n; }
    return r;
}
typedef struct { uint32_t id; uint64_t cost; } component;
static int cost_order(const void *a, const void *b) {
    const component *x = a, *y = b;
    if (x->cost != y->cost) return x->cost > y->cost ? -1 : 1;
    return x->id < y->id ? -1 : x->id != y->id;
}
static void pin(bool *pinned, uint32_t v) { if (v != RDS_NONE) pinned[v] = true; }
typedef struct { uint32_t to,next,weight; } affinity_edge;
/* Keep the existing balanced components, then exchange similar-cost regions
 * to reduce state-query and state-input traffic. This affects placement only;
 * it never creates replicas or changes a cycle's dependency order. */
static void affinity_place(const rds_sim*s,component*components,uint32_t nc,
                           uint32_t lanes,uint32_t*parent,const uint32_t*producer,
                           uint32_t*owner,uint64_t*loads){
    if(nc<2||nc>512)return;
    size_t capacity=s->no;for(uint32_t i=0;i<s->nx;++i)capacity+=s->objects[i].ni;
    if(capacity>UINT32_MAX/2)return;
    uint32_t *home=malloc((s->nx?s->nx:1)*sizeof *home),*head=malloc((s->no?s->no:1)*sizeof *head);
    affinity_edge*edges=malloc((capacity?capacity:1)*2*sizeof *edges);
    if(!home||!head||!edges){free(home);free(head);free(edges);return;}
    for(uint32_t i=0;i<s->nx;++i)home[i]=RDS_NONE;
    for(uint32_t i=0;i<s->no;++i)head[i]=RDS_NONE;
    for(uint32_t i=0;i<s->no;++i)if(s->ops[i].code==RDS_OBJECT_QUERY){
        uint32_t o=s->imm[s->ops[i].imm];
        if(home[o]==RDS_NONE||s->values[s->ops[i].out].width>s->values[s->ops[home[o]].out].width)home[o]=i;
    }
    uint32_t used=0;
#define CONNECT(a,b,w) do { uint32_t left=(a),right=(b),weight=(w); if(left!=RDS_NONE&&right!=RDS_NONE&&left!=right){ \
    edges[used]=(affinity_edge){right,head[left],weight};head[left]=used++; \
    edges[used]=(affinity_edge){left,head[right],weight};head[right]=used++; }}while(0)
    for(uint32_t i=0;i<s->no;++i)if(s->ops[i].code==RDS_OBJECT_QUERY){const rds_op*o=&s->ops[i];
        CONNECT(root(parent,home[s->imm[o->imm]]),root(parent,i),(s->values[o->out].width+7)/8);
    }
    for(uint32_t i=0;i<s->nx;++i)if(home[i]!=RDS_NONE){const rds_object*o=&s->objects[i];uint32_t r=root(parent,home[i]);
        for(uint32_t j=0;j<o->ni;++j){uint32_t v=o->inputs[j];if(v==RDS_NONE||producer[v]==RDS_NONE)continue;
            CONNECT(r,root(parent,producer[v]),(s->values[v].width+7)/8);
        }
    }
#undef CONNECT
    uint64_t cut=0;for(uint32_t a=0;a<nc;++a)for(uint32_t e=head[components[a].id];e!=RDS_NONE;e=edges[e].next)
        if(owner[components[a].id]!=owner[edges[e].to])cut+=edges[e].weight;
    s->schedule->affinity_before=cut/2;s->schedule->affinity_after=cut/2;
    uint64_t peak=0;for(uint32_t i=0;i<lanes;++i)if(loads[i]>peak)peak=loads[i];uint64_t limit=peak+peak/50;
    // A fixed iteration budget bounds construction time even for large graphs.
    for(unsigned pass=0;pass<64;++pass){int64_t best=0;uint32_t aa=RDS_NONE,bb=RDS_NONE;
        for(uint32_t a=0;a<nc;++a)for(uint32_t b=a+1;b<nc;++b){uint32_t x=components[a].id,y=components[b].id,lx=owner[x],ly=owner[y];
            if(lx==ly||loads[lx]-components[a].cost+components[b].cost>limit||loads[ly]-components[b].cost+components[a].cost>limit)continue;
            int64_t gain=0;for(unsigned side=0;side<2;++side){uint32_t id=side?y:x,from=side?ly:lx,to=side?lx:ly;
                for(uint32_t e=head[id];e!=RDS_NONE;e=edges[e].next){uint32_t other=edges[e].to;if(other==x||other==y)continue;
                    gain+=(int64_t)edges[e].weight*((owner[other]==to)-(owner[other]==from));
                }
            }
            if(gain>best){best=gain;aa=a;bb=b;}
        }
        if(aa==RDS_NONE)break;
        uint32_t x=components[aa].id,y=components[bb].id,lx=owner[x],ly=owner[y];
        ++s->schedule->affinity_swaps;s->schedule->affinity_after-=(uint64_t)best;
        loads[lx]=loads[lx]-components[aa].cost+components[bb].cost;loads[ly]=loads[ly]-components[bb].cost+components[aa].cost;owner[x]=ly;owner[y]=lx;
    }
    free(home);free(head);free(edges);
}
static uint32_t scalar_code(rds_sim *s, rds_op *o) {
    if (s->values[o->out].width > 64) return GENERIC;
    if (o->code == RDS_SLICE)
        return s->imm[o->imm] % 64 + s->values[o->out].width > 64 ? SLICE_CROSS : RDS_SLICE;
    for (uint32_t i = 0; i < o->nargs; ++i)
        if (s->values[s->args[o->args + i]].width > 64) return GENERIC;
    switch (o->code) {
    case RDS_COPY: case RDS_NOT: case RDS_AND: case RDS_OR: case RDS_XOR:
    case RDS_ADD: case RDS_SUB: case RDS_MUL: case RDS_EQ: case RDS_ULT:
    case RDS_SET_CLEAR: case RDS_BALANCE: case RDS_COUNTER_STEP:
    case RDS_SHL: case RDS_SHRU: case RDS_SLICE: case RDS_ZEXT: return o->code;
    case RDS_MUX:
        if (o->nargs == 3 && s->values[s->args[o->args]].width == 1 && s->imm[o->imm] == 1) return SELECT;
        return GENERIC;
    default: return GENERIC;
    }
}
/* A load-time ready queue groups independent operations without imposing a
 * global layer barrier. Queue storage is discarded before simulation starts. */
static int reorder(rds_sim *s, uint32_t *order, uint32_t count, uint32_t *producer) {
    enum { KEYS = (SLICE_CROSS + 2) * 65 };
    uint32_t heads[KEYS], tails[KEYS], sizes[KEYS] = {0};
    uint32_t *remaining = calloc(s->no ? s->no : 1, sizeof *remaining);
    uint32_t *next = malloc((s->no ? s->no : 1) * sizeof *next);
    uint32_t *offsets = calloc((size_t)s->no + 1, sizeof *offsets);
    uint32_t *edges = malloc((s->na ? s->na : 1) * sizeof *edges);
    uint32_t *cursor = malloc((s->no ? s->no : 1) * sizeof *cursor);
    if (!remaining || !next || !offsets || !edges || !cursor) {
        free(remaining); free(next); free(offsets); free(edges); free(cursor); return -1;
    }
    for (uint32_t k = 0; k < KEYS; ++k) heads[k] = tails[k] = RDS_NONE;
    for (uint32_t j = 0; j < count; ++j) {
        rds_op *o = &s->ops[order[j]];
        for (uint32_t a = 0; a < o->nargs; ++a) {
            uint32_t id = producer[s->args[o->args + a]];
            if (id != RDS_NONE) { ++offsets[id + 1]; ++remaining[order[j]]; }
        }
    }
    for (uint32_t i = 0; i < s->no; ++i) offsets[i + 1] += offsets[i];
    memcpy(cursor, offsets, (size_t)s->no * sizeof *cursor);
    for (uint32_t j = 0; j < count; ++j) {
        rds_op *o = &s->ops[order[j]];
        for (uint32_t a = 0; a < o->nargs; ++a) {
            uint32_t id = producer[s->args[o->args + a]];
            if (id != RDS_NONE) edges[cursor[id]++] = order[j];
        }
    }
#define ENQUEUE(id) do { \
    rds_op *op = &s->ops[id]; uint32_t code = scalar_code(s, op); \
    uint32_t key = code == GENERIC ? KEYS - 1 : code * 65 + s->values[op->out].width; \
    next[id] = RDS_NONE; \
    if (heads[key] == RDS_NONE) heads[key] = id; else next[tails[key]] = id; \
    tails[key] = id; ++sizes[key]; \
} while (0)
    for (uint32_t j = 0; j < count; ++j) if (!remaining[order[j]]) ENQUEUE(order[j]);
    uint32_t key = KEYS - 1;
    for (uint32_t j = 0; j < count; ++j) {
        if (!sizes[key]) {
            key = 0;
            for (uint32_t k = 1; k < KEYS; ++k) if (sizes[k] > sizes[key]) key = k;
        }
        if (!sizes[key]) { free(remaining); free(next); free(offsets); free(edges); free(cursor); return -1; }
        uint32_t id = heads[key]; heads[key] = next[id]; --sizes[key]; order[j] = id;
        for (uint32_t a = offsets[id]; a < offsets[id + 1]; ++a) {
            uint32_t user = edges[a];
            if (!--remaining[user]) ENQUEUE(user);
        }
    }
#undef ENQUEUE
    free(remaining); free(next); free(offsets); free(edges); free(cursor); return 0;
}
/* Exact-size free lists avoid allocator calls in the cycle loop. Each worker
 * owns separate scratch; only immutable inputs, constants, and q are shared. */
typedef struct { uint32_t words, head; } free_class;
static free_class *size_class(free_class *table, size_t capacity, uint32_t words) {
    size_t i = ((size_t)words * UINT32_C(2654435761)) & (capacity - 1);
    while (table[i].words && table[i].words != words) i = (i + 1) & (capacity - 1);
    if (!table[i].words) table[i] = (free_class){words, RDS_NONE};
    return table + i;
}
/* Sorted free intervals allow differently sized dead values to share storage.
 * Inputs die after output allocation, so generic kernels never alias operands. */
typedef struct { size_t offset, words; } free_span;
static size_t span_take(free_span *spans, size_t *count, size_t width, size_t *end) {
    size_t best = *count;
    for (size_t i = 0; i < *count; ++i)
        if (spans[i].words >= width && (best == *count || spans[i].words < spans[best].words)) best = i;
    if (best == *count) { size_t offset = *end; *end += width; return offset; }
    size_t offset = spans[best].offset;
    spans[best].offset += width; spans[best].words -= width;
    if (!spans[best].words) { --*count; memmove(spans + best, spans + best + 1, (*count - best) * sizeof *spans); }
    return offset;
}
static void span_release(free_span *spans, size_t *count, size_t offset, size_t width) {
    size_t i = 0;
    while (i < *count && spans[i].offset < offset) ++i;
    memmove(spans + i + 1, spans + i, (*count - i) * sizeof *spans);
    spans[i] = (free_span){offset, width}; ++*count;
    if (i && spans[i - 1].offset + spans[i - 1].words == spans[i].offset) {
        spans[i - 1].words += spans[i].words; --*count;
        memmove(spans + i, spans + i + 1, (*count - i) * sizeof *spans); --i;
    }
    if (i + 1 < *count && spans[i].offset + spans[i].words == spans[i + 1].offset) {
        spans[i].words += spans[i + 1].words; --*count;
        memmove(spans + i + 1, spans + i + 2, (*count - i - 1) * sizeof *spans);
    }
}
/* Scalar batches already contain offsets. Keep IR descriptors only for public
 * ports, state/effects, and the operations that still need the generic kernel. */
static int compact_descriptors(rds_sim *s, bool *keep, uint32_t *map, uint32_t *opmap) {
    rds_schedule *p = s->schedule;
    memset(keep, 0, (size_t)s->nv * sizeof *keep);
    for (uint32_t i = 0; i < s->nx; ++i)
        for (uint32_t j = 0; j < s->objects[i].ni; ++j) pin(keep,s->objects[i].inputs[j]);
    for (uint32_t i = 0; i < s->np; ++i) pin(keep,s->ports[i].value);
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *r = &s->regs[i]; pin(keep,r->q); pin(keep,r->d); pin(keep,r->reset); pin(keep,r->reset_value);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *w = &s->writes[i]; pin(keep,w->address); pin(keep,w->data); pin(keep,w->enable); pin(keep,w->mask);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *r = &s->reads[i]; pin(keep,r->address); pin(keep,r->enable); pin(keep,r->q); pin(keep,r->write);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        rds_assert *a = &s->checks[i]; pin(keep,a->condition); pin(keep,a->reset); pin(keep,a->guard);
    }
    uint32_t no = 0, na = 0, ni = 0, nv = 0;
    for (uint32_t i = 0; i < s->no; ++i) opmap[i] = RDS_NONE;
    for (uint32_t i = 0; i < s->batches; ++i) {
        batch *b = &p->batches[i];
        if (b->code != GENERIC) continue;
        for (uint32_t j = b->first; j < b->first + b->count; ++j) {
            uint32_t id = p->instructions[j].out;
            rds_op *o = &s->ops[id];
            opmap[id] = no++; na += o->nargs; ni += o->nimm; pin(keep,o->out);
            for (uint32_t k = 0; k < o->nargs; ++k) pin(keep,s->args[o->args + k]);
        }
    }
    for (uint32_t i = 0; i < s->nv; ++i) if (keep[i]) map[i] = nv++;
    rds_value *v = calloc(nv ? nv : 1, sizeof *v);
    rds_op *ops = calloc(no ? no : 1, sizeof *ops);
    uint32_t *args = calloc(na ? na : 1, sizeof *args);
    uint64_t *imm = calloc(ni ? ni : 1, sizeof *imm);
    if (!v || !ops || !args || !imm) { free(v); free(ops); free(args); free(imm); return -1; }
    for (uint32_t i = 0; i < s->nv; ++i) if (keep[i]) v[map[i]] = s->values[i];
    uint32_t ai = 0, ii = 0;
    for (uint32_t i = 0; i < s->no; ++i) if (opmap[i] != RDS_NONE) {
        rds_op *o = &s->ops[i];
        ops[opmap[i]] = (rds_op){o->code,map[o->out],ai,o->nargs,ii,o->nimm};
        for (uint32_t j = 0; j < o->nargs; ++j) args[ai++] = map[s->args[o->args + j]];
        for (uint32_t j = 0; j < o->nimm; ++j) imm[ii++] = s->imm[o->imm + j];
    }
    for (uint32_t i = 0; i < s->batches; ++i) {
        batch *b = &p->batches[i];
        if (b->code == GENERIC) for (uint32_t j = b->first; j < b->first + b->count; ++j)
            p->instructions[j].out = opmap[p->instructions[j].out];
    }
#define REMAP(field) do { if ((field) != RDS_NONE) (field) = map[field]; } while (0)
    for (uint32_t i = 0; i < s->nx; ++i)
        for (uint32_t j = 0; j < s->objects[i].ni; ++j) REMAP(s->objects[i].inputs[j]);
    for (uint32_t i = 0; i < s->np; ++i) REMAP(s->ports[i].value);
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *r = &s->regs[i]; REMAP(r->q); REMAP(r->d); REMAP(r->reset); REMAP(r->reset_value);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *w = &s->writes[i]; REMAP(w->address); REMAP(w->data); REMAP(w->enable); REMAP(w->mask);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *r = &s->reads[i]; REMAP(r->address); REMAP(r->enable); REMAP(r->q); REMAP(r->write);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        rds_assert *a = &s->checks[i]; REMAP(a->condition); REMAP(a->reset); REMAP(a->guard);
    }
#undef REMAP
    free(s->values); free(s->ops); free(s->args); free(s->imm);
    s->values = v; s->ops = ops; s->args = args; s->imm = imm;
    s->nv = nv; s->no = no; s->na = na; s->ni = ni;
    return 0;
}
int rds_schedule_build(rds_sim *s, const rds_options *options) {
    uint32_t requested = options && options->workers ? options->workers : 1;
    uint32_t flags = options ? options->flags : 0;
    s->workers = 1;
    s->set_clear = rds_set_clear_c;
    s->copy_state = (flags & RDS_COPY_STATE) != 0;
    s->shared_code = (flags & RDS_SHARED_CODE) != 0;
    s->parallel_publish = (flags & RDS_PARALLEL_PUBLISH) != 0;
    s->inline_bodies = flags & RDS_INLINE_BODIES ? 2 : flags & RDS_AUTO_INLINE ? 1 : 0;
    s->linear_decode = (flags & RDS_LINEAR_DECODE) != 0;
    s->eager_combinational = (flags & RDS_EAGER_COMBINATIONAL) != 0;
    s->wide_regions = (flags & RDS_WIDE_REGIONS) != 0;
    s->select_regions = (flags & RDS_SELECT_REGIONS) != 0;
    s->guarded_onehot = (flags & RDS_GUARDED_ONEHOT) != 0;
    s->cold_regions = (flags & RDS_COLD_REGIONS) != 0;
    s->flow_prepare = (flags & RDS_FLOW_PREPARE) != 0;
    s->lift_transitions = (flags & RDS_LIFT_TRANSITIONS) != 0;
    s->lift_primitives = (flags & RDS_LIFT_PRIMITIVES) != 0;
    s->materialize_state = (flags & RDS_DIRECT_STATE_PACKS) == 0;
    if (requested > 256 || (flags & ~(RDS_REFERENCE | RDS_NO_REUSE | RDS_NO_BATCH | RDS_USE_ASM | RDS_NO_REORDER | RDS_NO_PARTITION | RDS_SPIN | RDS_COPY_STATE | RDS_SHARED_CODE | RDS_MORE_REPLICATION | RDS_PARALLEL_PUBLISH | RDS_INLINE_BODIES | RDS_AUTO_INLINE | RDS_LINEAR_DECODE | RDS_PARALLEL_STATE | RDS_EAGER_COMBINATIONAL | RDS_DIRECT_STATE_PACKS | RDS_COALESCE_SCRATCH | RDS_WIDE_REGIONS | RDS_SELECT_REGIONS | RDS_SEMANTIC_REGIONS | RDS_STABLE_PAYLOADS | RDS_BALANCE_PARTITIONS | RDS_STABLE_FIELDS | RDS_GUARDED_ONEHOT | RDS_UNION_DEMAND | RDS_FLOW_CACHE | RDS_COLD_REGIONS | RDS_FLOW_PREPARE | RDS_LIFT_TRANSITIONS | RDS_LIFT_PRIMITIVES | RDS_TYPED_SCRATCH))) return -1;
    if (flags & RDS_USE_ASM) {
#ifdef RDS_HAVE_X86_64_ASM
        s->set_clear = rds_set_clear_x86_64;
#else
        return -1;
#endif
    }
    if (flags & RDS_REFERENCE) return requested == 1 ? 0 : -1;
    if (rds_partition(s, requested, flags) < 0) return -1;
    size_t n = s->no ? s->no : 1, nv = s->nv ? s->nv : 1;
    uint32_t *producer = malloc(nv * sizeof *producer), *parent = malloc(n * sizeof *parent);
    uint32_t *owner = calloc(n, sizeof *owner), *order = malloc(n * sizeof *order);
    uint32_t *guards = NULL;
    uint32_t *cache = NULL;
    uint32_t *last = calloc(nv, sizeof *last), *next = malloc(nv * sizeof *next);
    bool *pinned = calloc(nv, sizeof *pinned), *released = calloc(nv, sizeof *released);
    bool *tail = calloc(n, sizeof *tail), *candidate_tail = calloc(nv, sizeof *candidate_tail);
    component *components = calloc(n, sizeof *components);
    uint64_t *cost = calloc(n, sizeof *cost), loads[256] = {0};
    size_t capacity = 2;
    while (capacity < nv * 2) capacity *= 2;
    free_class *free_slots = calloc(capacity, sizeof *free_slots);
    free_span *spans = calloc(nv, sizeof *spans);
    /* Workers continuously read the command epoch. Keep it off the cache lines
     * containing caller-written phase and bulk bookkeeping. */
    rds_schedule *p = aligned_alloc(64, sizeof *p);
    if(p)memset(p,0,sizeof *p);
    int result = -1;
    if (!producer || !parent || !owner || !order || !last || !next || !pinned || !released || !tail || !candidate_tail
        || !components || !cost || !free_slots || !spans || !p) goto finish;
    s->schedule = p; p->sim = s;
    p->spin = (flags & RDS_SPIN) != 0; atomic_init(&p->epoch, 0);
    for (uint32_t i = 0; i < s->nv; ++i) producer[i] = RDS_NONE;
    for (uint32_t i = 0; i < s->no; ++i) {
        parent[i] = i;
        if (s->ops[i].code != RDS_CONST) producer[s->ops[i].out] = i;
    }
    for (uint32_t i = 0; i < s->nv; ++i) pinned[i] = producer[i] == RDS_NONE;
    /* A small broadcast readiness reduction can join large independent
     * producer cones. Its closed consumer tail runs after the existing eval
     * barrier, before any preparation, observation, or state publication. */
    if (requested>1 && !s->partition_owners) for (uint32_t seed=0;seed<s->no;++seed) {
        const rds_op *o=&s->ops[seed];
        if (o->code!=RDS_OBJECT_QUERY || s->imm[o->imm+1]!=0 ||
            s->objects[s->imm[o->imm]].kind!=9) continue;
        memset(candidate_tail,0,nv*sizeof *candidate_tail);
        candidate_tail[o->out]=true;
        uint32_t size=0;bool total=true;
        for (uint32_t i=seed;i<s->no;++i) {
            o=&s->ops[i];
            for (uint32_t j=0;j<o->nargs;++j)
                candidate_tail[o->out]|=candidate_tail[s->args[o->args+j]];
            if (candidate_tail[o->out]) {
                ++size;
                if (o->code==RDS_ONEHOT || o->code==RDS_ONEHOT_VIEW || (o->code>=RDS_INDEX && o->code<=RDS_READ)) total=false;
            }
        }
        if (total && size<=256) for(uint32_t i=seed;i<s->no;++i)
            if(candidate_tail[s->ops[i].out])tail[i]=true;
    }
    for(uint32_t i=0;i<s->no;++i)if(tail[i]) {
        pin(pinned,s->ops[i].out);
        for(uint32_t j=0;j<s->ops[i].nargs;++j)pin(pinned,s->args[s->ops[i].args+j]);
    }
    for (uint32_t i = 0; i < s->nx; ++i)
        for (uint32_t j = 0; j < s->objects[i].ni; ++j) pin(pinned,s->objects[i].inputs[j]);
    for (uint32_t i = 0; i < s->np; ++i) pin(pinned, s->ports[i].value);
    for (uint32_t i = 0; i < s->nr; ++i) {
        rds_reg *r = &s->regs[i]; pin(pinned,r->d); pin(pinned,r->reset); pin(pinned,r->reset_value);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
        rds_write *w = &s->writes[i]; pin(pinned,w->address); pin(pinned,w->data); pin(pinned,w->enable); pin(pinned,w->mask);
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
        rds_read *r = &s->reads[i]; pin(pinned,r->address); pin(pinned,r->enable); pin(pinned,r->write);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
        rds_assert *a = &s->checks[i]; pin(pinned,a->condition); pin(pinned,a->reset); pin(pinned,a->guard);
    }
    for (uint32_t i = 0; i < s->no; ++i) {
        rds_op *o = &s->ops[i];
        if (o->code == RDS_CONST || tail[i]) continue;
        for (uint32_t j = 0; j < o->nargs; ++j) {
            uint32_t other = producer[s->args[o->args + j]];
            if (other != RDS_NONE && !tail[other]) {
                uint32_t a = root(parent, i), b = root(parent, other);
                if (a != b) parent[a > b ? a : b] = a < b ? a : b;
            }
        }
    }
    for (uint32_t i = 0; i < s->no; ++i) {
        rds_op *o = &s->ops[i];
        if (o->code != RDS_CONST && !tail[i]) {
            uint64_t words = s->values[o->out].words;
            cost[root(parent, i)] += o->code == RDS_MUL ? words * words : words + o->nargs;
        }
    }
    uint32_t nc = 0;
    for (uint32_t i = 0; i < s->no; ++i) if (cost[i]) components[nc++] = (component){i,cost[i]};
    qsort(components, nc, sizeof *components, cost_order);
    uint32_t parallelism = nc > s->nx ? nc : s->nx;
    p->count = parallelism && requested > parallelism ? parallelism : requested;
    if (!parallelism) p->count = 1;
    s->workers = p->count; s->components = nc;
    for (uint32_t i = 0; i < nc; ++i) {
        uint32_t lane = 0;
        for (uint32_t j = 1; j < p->count; ++j) if (loads[j] < loads[lane]) lane = j;
        owner[components[i].id] = lane; loads[lane] += components[i].cost;
    }
    bool affinity_safe=true;for(uint32_t i=0;i<s->no;++i)if(tail[i])affinity_safe=false;
    if((flags&RDS_BALANCE_PARTITIONS)&&p->count>1&&!s->partition_owners&&affinity_safe)
        affinity_place(s,components,nc,p->count,parent,producer,owner,loads);
    for (uint32_t i = 0; i < s->no; ++i) owner[i] = owner[root(parent,i)];
    if(p->count==1)memset(tail,0,n*sizeof *tail);
    for(uint32_t i=0;i<s->no;++i)if(tail[i])owner[i]=RDS_NONE;
    if (s->partition_owners) {
        p->count = s->partition_workers; s->workers = p->count; s->components = p->count;
        memcpy(owner, s->partition_owners, (size_t)s->no * sizeof *owner);
    }
    if(!s->partition_owners){
        /* Preparation follows the eval barrier, so its owner can follow the
         * dominant snapshot reader. Keep mutable object storage near the core
         * that reads it instead of striping every core across all workers. */
        uint64_t *readers=calloc((size_t)(s->nx?s->nx:1)*p->count,sizeof *readers);
        if(!readers)goto finish;
        uint64_t publication[256]={0};
        for(uint32_t i=0;i<s->no;++i){const rds_op *o=&s->ops[i];
            if(o->code==RDS_OBJECT_QUERY && owner[i]!=RDS_NONE)
                readers[(size_t)s->imm[o->imm]*p->count+owner[i]]+=3+s->values[o->out].words;
        }
        for(uint32_t id=0;id<s->nx;++id){
            uint32_t best=0;uint64_t *votes=readers+(size_t)id*p->count;
            for(uint32_t lane=1;lane<p->count;++lane)
                if(votes[lane]>votes[best] || (votes[lane]==votes[best] && publication[lane]<publication[best]))best=lane;
            s->objects[id].owner=best;publication[best]+=3+s->objects[id].words;
        }
        free(readers);
    }
    /* A register can stage on any worker owning all of its computed inputs,
     * including a component schedule without replicated cones. Cross-worker
     * sinks remain serial. Separate each writer's next-bank cache lines. */
    if(flags&RDS_PARALLEL_STATE) {
        for(uint32_t i=0;i<s->nr;++i) {
            rds_reg *r=&s->regs[i];uint32_t fields[]={r->d,r->reset,r->reset_value};
            uint32_t lane=RDS_NONE;bool conflict=false;
            for(unsigned j=0;j<3;++j)if(fields[j]!=RDS_NONE && producer[fields[j]]!=RDS_NONE) {
                uint32_t candidate=owner[producer[fields[j]]];
                if(candidate==RDS_NONE)conflict=true;
                if(lane!=RDS_NONE && lane!=candidate)conflict=true;
                lane=candidate;
            }
            if(!conflict)r->owner=lane==RDS_NONE?0:lane;
        }
        size_t words=0;
        for(uint32_t lane=0;lane<=p->count;++lane) {
            words=(words+7)&~(size_t)7;
            for(uint32_t i=0;i<s->nr;++i)if(s->regs[i].owner==(lane==p->count?RDS_NONE:lane)) {
                s->regs[i].next=words;words+=s->values[s->regs[i].q].words;
            }
        }
        for(uint32_t i=0;i<s->ns;++i){s->reads[i].next=words;words+=s->values[s->reads[i].q].words;}
        uint64_t *next_bank=rds_state_bank(words);
        if(!next_bank)goto finish;
        free(s->next);s->next=next_bank;s->next_words=words;
    }
    memset(loads, 0, sizeof loads); s->partition_work = s->partition_peak = 0;
    for (uint32_t i = 0; i < s->no; ++i) if(!tail[i]) loads[owner[i]] += rds_operation_cost(s, &s->ops[i]);
    for (uint32_t lane = 0; lane < p->count; ++lane) {
        s->partition_work += loads[lane];
        if (loads[lane] > s->partition_peak) s->partition_peak = loads[lane];
    }
    uint32_t count = 0, starts[257] = {0};
    for (uint32_t lane = 0; lane < p->count; ++lane) {
        for (uint32_t i = 0; i < s->no; ++i)
            if (s->ops[i].code != RDS_CONST && owner[i] == lane) order[count++] = i;
        starts[lane + 1] = count;
    }
    if ((flags & RDS_SEMANTIC_REGIONS) && !s->eager_combinational && p->count == 1) {
        if (flags & RDS_UNION_DEMAND) {
            p->guard_unions = calloc(RDS_GUARD_OR_LIMIT, sizeof *p->guard_unions);
            if (!p->guard_unions) goto finish;
        }
        guards = calloc(n, sizeof *guards);
        if (!guards || rds_semantic_regions(s, order, count, producer, owner, pinned, guards)) goto finish;
    }
    if ((flags & (RDS_STABLE_PAYLOADS | RDS_STABLE_FIELDS | RDS_FLOW_CACHE)) && !s->eager_combinational) {
        cache=malloc(n*sizeof *cache);p->cache_objects=calloc(s->nx?s->nx:1,sizeof *p->cache_objects);
        p->cache_lanes=calloc((size_t)p->count*(s->nx?s->nx:1),sizeof *p->cache_lanes);
        if(!cache||!p->cache_objects||!p->cache_lanes)goto finish;
        if((flags&RDS_STABLE_FIELDS)&&p->count==1){
            p->cache_fields=calloc(s->nr?s->nr:1,sizeof *p->cache_fields);
            p->field_masks=calloc(s->next_words?s->next_words:1,sizeof *p->field_masks);
            if(!p->cache_fields||!p->field_masks)goto finish;
        }
        for(uint32_t lane=0;lane<p->count;++lane){
            bool *objects=p->cache_lanes+(size_t)lane*s->nx;
            if(rds_stable_payloads(s,order+starts[lane],starts[lane+1]-starts[lane],pinned,cache,objects,p->cache_fields))goto finish;
            for(uint32_t i=0;i<s->nx;++i)p->cache_objects[i]|=objects[i];
        }
        if ((flags & RDS_FLOW_CACHE) && p->count==1 &&
            rds_flow_cache(s,order,count,pinned,cache,guards)) goto finish;
        if(p->cache_fields){
            bool used=false;for(uint32_t r=0;r<s->nr;++r)used|=p->cache_fields[r];
            if(!used){free(p->cache_fields);p->cache_fields=NULL;free(p->field_masks);p->field_masks=NULL;}
        }
    } else if (!guards && !(flags & RDS_NO_REORDER)) for (uint32_t lane = 0; lane < p->count; ++lane)
        if (reorder(s, order + starts[lane], starts[lane + 1] - starts[lane], producer)) goto finish;
    p->tail_first=count;
    for(uint32_t i=0;i<s->no;++i)if(tail[i]){order[count++]=i;++p->tail_count;}
    for (uint32_t i = 0; i < count; ++i) {
        rds_op *o = &s->ops[order[i]];
        last[o->out] = i;
        for (uint32_t j = 0; j < o->nargs; ++j) last[s->args[o->args + j]] = i;
    }
    if (!(flags & RDS_NO_REUSE)) {
        size_t words = 0;
        for (uint32_t i = 0; i < s->nv; ++i) if (producer[i] == RDS_NONE) {
            s->values[i].offset = words; words += s->values[i].words;
        }
        for (uint32_t lane = 0; lane < p->count; ++lane) {
            words = (words + 7) & ~(size_t)7;
            memset(free_slots, 0, capacity * sizeof *free_slots);
            /* Keep all versions of a reusable narrow scalar slot byte-sized
             * for the later C emitter. Coalescing retains its span allocator. */
            free_class narrow_slots = {1, RDS_NONE};
            bool typed = (flags & RDS_TYPED_SCRATCH) && !(flags & RDS_COALESCE_SCRATCH);
            size_t span_count = 0;
            for (uint32_t i = starts[lane]; i < starts[lane + 1]; ++i) {
                uint32_t id = s->ops[order[i]].out;
                if (pinned[id]) { s->values[id].offset = words; words += s->values[id].words; }
            }
            for (uint32_t i = starts[lane]; i < starts[lane + 1]; ++i) {
                rds_op *o = &s->ops[order[i]];
                if (!pinned[o->out]) {
                    rds_value *v = &s->values[o->out];
                    free_class *cl = typed && v->words == 1 && v->width <= 8
                        ? &narrow_slots : size_class(free_slots, capacity, v->words);
                    if (flags & RDS_COALESCE_SCRATCH) v->offset = span_take(spans, &span_count, v->words, &words);
                    else if (cl->head == RDS_NONE) { v->offset = words; words += v->words; }
                    else { uint32_t old = cl->head; cl->head = next[old]; v->offset = s->values[old].offset; }
                }
                for (uint32_t j = 0; j <= o->nargs; ++j) {
                    uint32_t id = j == o->nargs ? o->out : s->args[o->args + j];
                    if (!pinned[id] && !released[id] && last[id] == i) {
                        const rds_value *v = &s->values[id];
                        free_class *cl = typed && v->words == 1 && v->width <= 8
                            ? &narrow_slots : size_class(free_slots, capacity, v->words);
                        if (flags & RDS_COALESCE_SCRATCH) span_release(spans, &span_count, s->values[id].offset, s->values[id].words);
                        else { next[id] = cl->head; cl->head = id; }
                        released[id] = true;
                    }
                }
            }
        }
        for(uint32_t i=p->tail_first;i<count;++i){
            rds_value *v=&s->values[s->ops[order[i]].out];
            v->offset=words;words+=v->words;
        }
        size_t bytes = ((words ? words : 1) * 8 + 63) & ~(size_t)63;
        uint64_t *arena = aligned_alloc(64, bytes);
        if (!arena) goto finish;
        memset(arena, 0, bytes);
        free(s->arena); s->arena = arena; s->value_words = bytes / 8;
    }
    for (uint32_t i = 0; i < s->no; ++i) if (s->ops[i].code == RDS_CONST) ++p->constant_count;
    p->constants = calloc(p->constant_count ? p->constant_count : 1, sizeof *p->constants);
    if (!p->constants) goto finish;
    uint32_t constant_index = 0;
    for (uint32_t i = 0; i < s->no; ++i) if (s->ops[i].code == RDS_CONST) {
        if (rds_execute(s, &s->ops[i])) goto finish;
        rds_value *v = &s->values[s->ops[i].out];
        p->constants[constant_index++] = (constant_span){v->offset, v->words};
    }
    p->workers = calloc(p->count, sizeof *p->workers);
    p->instructions = calloc(count ? count : 1, sizeof *p->instructions);
    p->instruction_origins = calloc(count ? count : 1, sizeof *p->instruction_origins);
    if (guards) p->instruction_guards = calloc(count ? count : 1, sizeof *p->instruction_guards);
    if (cache) p->instruction_cache = malloc((count ? count : 1)*sizeof *p->instruction_cache);
    p->batches = calloc(count ? count : 1, sizeof *p->batches);
    if (!p->workers || !p->instructions || !p->instruction_origins || !p->batches || (guards && !p->instruction_guards) || (cache && !p->instruction_cache)) goto finish;
    p->prepare_local = p->count == 1 || s->partition_owners != NULL;
    /* Keep the predicate after ownership is final and before attachment releases
     * descriptors. Host callbacks require the ordinary transactional ordering;
     * a broadcast epilogue requires another dependency boundary. */
    p->bulk_safe=!p->tail_count;
    for(uint32_t i=0;i<s->nx;++i)if(s->objects[i].kind==8)p->bulk_safe=false;
    p->bulk_local=p->prepare_local&&!s->nc&&!s->nw&&!s->ns;
    /* Without checks or memory effects, remaining serial register proposals are
     * infallible copies from immutable arena/current-bank values. Worker zero
     * can stage these alongside object publication after all readers finish. */
    atomic_init(&p->bulk_failure_phase,UINT64_MAX);
    if(p->count>1){
        p->bulk_progress=aligned_alloc(64,(size_t)p->count*sizeof *p->bulk_progress);
        if(!p->bulk_progress)goto finish;
        for(uint32_t i=0;i<p->count;++i)atomic_init(&p->bulk_progress[i].epoch,0);
    }
    for (uint32_t lane = 0; lane < p->count; ++lane) {
        worker *w = &p->workers[lane]; w->plan = p; w->first = s->batches; w->id=lane;
        if (p->prepare_local) {
            for (uint32_t j = 0; j < s->nx; ++j) if (s->objects[j].owner == lane) ++w->object_count;
            w->objects = calloc(w->object_count ? w->object_count : 1, sizeof *w->objects);
            if (!w->objects) goto finish;
            uint32_t used = 0;
            for (uint32_t j = 0; j < s->nx; ++j) if (s->objects[j].owner == lane) w->objects[used++] = j;
        }
        for (uint32_t i = starts[lane]; i < starts[lane + 1]; ++i) {
            rds_op *o = &s->ops[order[i]];
            uint32_t code = (flags & RDS_NO_BATCH) || s->value_words > UINT32_MAX ? GENERIC : scalar_code(s,o);
            uint32_t width = code == GENERIC ? 1 : s->values[o->out].width;
            instruction ins = {order[i],0,0,0};
            if (code != GENERIC) {
                ins.out = (uint32_t)s->values[o->out].offset;
                ins.a = (uint32_t)s->values[s->args[o->args]].offset;
                if (o->nargs > 1) ins.b = (uint32_t)s->values[s->args[o->args + 1]].offset;
                if (o->nargs > 2) ins.c = (uint32_t)s->values[s->args[o->args + 2]].offset;
                if (code == RDS_SLICE || code == SLICE_CROSS) {
                    ins.a += (uint32_t)(s->imm[o->imm] / 64);
                    ins.c = (uint32_t)(s->imm[o->imm] % 64);
                }
            }
            p->instructions[i] = ins;
            p->instruction_origins[i] = s->values[o->out].origin;
            if(cache)p->instruction_cache[i]=cache[order[i]];
            if (guards && guards[order[i]]) {
                uint32_t g = guards[order[i]], v = (g - 2) / 2;
                if (g & RDS_GUARD_OR) p->instruction_guards[i] = g;
                else {
                    if ((uint64_t)s->nx + s->nw + 3 + 2 * s->values[v].offset >= RDS_GUARD_OR) goto finish;
                    p->instruction_guards[i] = s->nx + s->nw + 1 + 2 * (uint32_t)s->values[v].offset + (g & 1);
                }
            }
            if (!w->count || p->batches[s->batches - 1].code != code || p->batches[s->batches - 1].width != width) {
                p->batches[s->batches++] = (batch){code,width,i,1}; ++w->count;
            } else ++p->batches[s->batches - 1].count;
        }
    }
    for (uint32_t i = 0; i < p->guard_union_count; ++i)
        for (uint32_t j = 0; j < p->guard_unions[i].count; ++j) {
            uint32_t g = p->guard_unions[i].atoms[j], v = (g - 2) / 2;
            if ((uint64_t)s->nx + s->nw + 3 + 2 * s->values[v].offset >= RDS_GUARD_OR) goto finish;
            p->guard_unions[i].atoms[j] = s->nx + s->nw + 1 + 2 * (uint32_t)s->values[v].offset + (g & 1);
        }
    for(uint32_t i=p->tail_first;i<count;++i){
        const rds_op *o=&s->ops[order[i]];
        p->instructions[i]=(instruction){order[i],0,0,0};
        p->instruction_origins[i]=s->values[o->out].origin;
        if(p->instruction_cache)p->instruction_cache[i]=RDS_NONE;
        p->batches[s->batches++]=(batch){GENERIC,s->values[o->out].width,i,1};
    }
    p->offers=rds_offer_build(s);
    if (compact_descriptors(s, pinned, producer, parent)) goto finish;
    batch *small = realloc(p->batches, (s->batches ? s->batches : 1) * sizeof *p->batches);
    if (small) p->batches = small;
    for (uint32_t i = 0; i < p->count; ++i) p->workers[i].view = *s;
    s->schedule_bytes = sizeof *p + (size_t)p->count*sizeof *p->workers
        + (size_t)(count ? count : 1)*(sizeof *p->instructions + sizeof *p->instruction_origins)
        + (size_t)(small ? (s->batches ? s->batches : 1) : (count ? count : 1))*sizeof *p->batches
        + (size_t)(p->constant_count ? p->constant_count : 1) * sizeof *p->constants;
    if(p->bulk_progress)s->schedule_bytes+=(size_t)p->count*sizeof *p->bulk_progress;
    if(p->guard_unions)s->schedule_bytes+=(size_t)RDS_GUARD_OR_LIMIT*sizeof *p->guard_unions;
    if(p->instruction_guards)s->schedule_bytes+=(size_t)(count?count:1)*sizeof *p->instruction_guards;
    if(p->cache_fields)s->schedule_bytes+=(s->nr?s->nr:1)*sizeof *p->cache_fields+(s->next_words?s->next_words:1)*sizeof *p->field_masks;
    if(p->instruction_cache)s->schedule_bytes+=(size_t)(count?count:1)*sizeof *p->instruction_cache+(s->nx?s->nx:1)*(1+(size_t)p->count)*sizeof *p->cache_objects;
    if(p->flow_cache_objects)s->schedule_bytes+=(s->nx?s->nx:1)*sizeof *p->flow_cache_objects;
    if (p->prepare_local) for (uint32_t lane = 0; lane < p->count; ++lane)
        s->schedule_bytes += 4ull * (p->workers[lane].object_count ? p->workers[lane].object_count : 1);
    free(s->partition_owners); s->partition_owners = NULL;
    if (p->count > 1) {
        if (p->spin) {
            _Static_assert(sizeof(completion) == 64, "one completion per cache line");
            p->completions = aligned_alloc(64, (size_t)p->count * sizeof *p->completions);
            if (!p->completions) goto finish;
            for (uint32_t i = 0; i < p->count; ++i) atomic_init(&p->completions[i].epoch, 0);
            s->schedule_bytes += (size_t)p->count * sizeof *p->completions;
        }
        if (pthread_mutex_init(&p->gate, NULL)) goto finish;
        if (pthread_cond_init(&p->gate_changed, NULL)) { pthread_mutex_destroy(&p->gate); goto finish; }
        if (pthread_barrier_init(&p->start, NULL, p->count)) {
            pthread_cond_destroy(&p->gate_changed); pthread_mutex_destroy(&p->gate); goto finish;
        }
        if (pthread_barrier_init(&p->done, NULL, p->count)) {
            pthread_barrier_destroy(&p->start); pthread_cond_destroy(&p->gate_changed); pthread_mutex_destroy(&p->gate); goto finish;
        }
        p->synchronization = true;
        for (uint32_t i = 1; i < p->count; ++i) {
            if (pthread_create(&p->workers[i].thread, NULL, worker_main, &p->workers[i])) goto finish;
            ++p->created;
        }
        pthread_mutex_lock(&p->gate); p->ready = 1;
        pthread_cond_broadcast(&p->gate_changed); pthread_mutex_unlock(&p->gate);
    }
    result = 0;
finish:
    free(tail);free(candidate_tail);
    free(guards);
    free(cache);
    free(producer); free(parent); free(owner); free(order); free(last); free(next);
    free(pinned); free(released); free(components); free(cost); free(free_slots); free(spans);
    if (result) { if (s->schedule) rds_schedule_free(s); else free(p); }
    return result;
}
