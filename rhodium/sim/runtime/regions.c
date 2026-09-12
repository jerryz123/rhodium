/* Derives static consumer demand before scratch allocation and groups event
 * work. */
#define _POSIX_C_SOURCE 200809L
#include "schedule.h"

/* Zero means eager, NONE means no consumer yet, and other values encode a
 * Boolean literal or flat disjunction. Only earlier controls may guard work;
 * this keeps both explicit and added dependencies acyclic by construction. */
static uint32_t atom(uint32_t value, bool positive) {
  return 2 + 2 * value + positive;
}
static void factors(const rds_sim *s, const uint32_t *producer, uint32_t guard,
                    uint32_t *out, unsigned *count, unsigned depth) {
  if (*count == 16 || !depth)
    return;
  for (unsigned i = 0; i < *count; ++i)
    if (out[i] == guard)
      return;
  out[(*count)++] = guard;
  uint32_t value = (guard - 2) / 2, p = producer[value];
  bool positive = guard & 1;
  if (p == RDS_NONE || s->values[value].width != 1)
    return;
  const rds_op *op = &s->ops[p];
  const uint32_t *a = s->args + op->args;
  if (op->code == RDS_COPY || op->code == RDS_NOT)
    factors(s, producer, atom(a[0], positive ^ (op->code == RDS_NOT)), out,
            count, depth - 1);
  if ((op->code == RDS_AND && positive) || (op->code == RDS_OR && !positive)) {
    factors(s, producer, atom(a[0], positive), out, count, depth - 1);
    factors(s, producer, atom(a[1], positive), out, count, depth - 1);
  }
}
/* Canonicalize a bounded disjunction. Exhaustion conservatively makes work
 * eager; the pool is built once and does not survive compiled attachment. */
static uint32_t canonical_atom(const rds_sim *s, const uint32_t *producer,
                               uint32_t guard) {
  for (unsigned depth = 0; depth < 8; ++depth) {
    uint32_t v = (guard - 2) / 2, id = producer[v];
    if (id == RDS_NONE) {
      // Constant definitions are excluded from the scheduled producer map.
      for (uint32_t i = 0; i < s->no; ++i)
        if (s->ops[i].code == RDS_CONST && s->ops[i].out == v) {
          bool nonzero = false;
          for (uint32_t j = 0; j < s->ops[i].nimm; ++j)
            nonzero |= s->imm[s->ops[i].imm + j] != 0;
          return nonzero == !!(guard & 1) ? 0 : RDS_NONE;
        }
      break;
    }
    const rds_op *op = &s->ops[id];
    if (s->values[v].width != 1 || (op->code != RDS_COPY && op->code != RDS_NOT)) break;
    guard = atom(s->args[op->args], (guard & 1) ^ (op->code == RDS_NOT));
  }
  return guard;
}
static uint32_t union_guard(const rds_sim *s, const uint32_t *producer,
                            uint32_t left, uint32_t right) {
  rds_schedule *p = s->schedule;
  if (!p->guard_unions) return 0;
  rds_guard_or candidate = {0};
  uint32_t input[2] = {left, right};
  for (unsigned j = 0; j < 2; ++j) {
    uint32_t count;
    const uint32_t *terms = rds_guard_atoms(p, &input[j], &count);
    for (uint32_t k = 0; k < count; ++k) {
      uint32_t term = canonical_atom(s, producer, terms[k]), at = 0;
      if (!term) return 0;
      if (term == RDS_NONE) continue;
      while (at < candidate.count && candidate.atoms[at] < term) ++at;
      if (at < candidate.count && candidate.atoms[at] == term) continue;
      for (uint32_t q = 0; q < candidate.count; ++q)
        if ((candidate.atoms[q] ^ term) == 1) return 0;
      if (candidate.count == RDS_GUARD_OR_TERMS) return 0;
      memmove(candidate.atoms + at + 1, candidate.atoms + at,
              (candidate.count - at) * sizeof *candidate.atoms);
      candidate.atoms[at] = term;
      ++candidate.count;
    }
  }
  if (candidate.count == 1) return candidate.atoms[0];
  for (uint32_t i = 0; i < p->guard_union_count; ++i)
    if (!memcmp(&candidate, &p->guard_unions[i], sizeof candidate))
      return RDS_GUARD_OR | i;
  if (p->guard_union_count == RDS_GUARD_OR_LIMIT) return 0;
  p->guard_unions[p->guard_union_count] = candidate;
  return RDS_GUARD_OR | p->guard_union_count++;
}
static void need(const rds_sim *s, const uint32_t *producer, uint32_t *demand,
                 uint32_t value, uint32_t guard) {
  if (value == RDS_NONE)
    return;
  uint32_t old = demand[value];
  if (old == RDS_NONE || old == guard) {
    demand[value] = guard;
    return;
  }
  demand[value] = 0;
  if (!old || !guard)
    return;
  // (valid && a) OR (valid && b) needs at most the common valid domain.
  // This is syntactic implication, not an assumed reachable-state invariant.
  uint32_t left[16], right[16];
  unsigned nl = 0, nr = 0;
  if (!(old & RDS_GUARD_OR)) factors(s, producer, old, left, &nl, 8);
  if (!(guard & RDS_GUARD_OR)) factors(s, producer, guard, right, &nr, 8);
  for (unsigned i = 0; i < nl; ++i)
    for (unsigned j = 0; j < nr; ++j)
      if (left[i] == right[j]) {
        demand[value] = left[i];
        return;
      }
  demand[value] = union_guard(s, producer, old, guard);
}
static bool index_total(const rds_sim *s, uint32_t value, uint64_t depth) {
  uint32_t width = s->values[value].width;
  return width < 64 && (UINT64_C(1) << width) <= depth;
}
static bool partial(const rds_sim *s, const rds_op *op) {
  const uint32_t *a = s->args + op->args;
  if (op->code == RDS_INDEX || op->code == RDS_INJECT)
    return !index_total(s, a[1], s->imm[op->imm]);
  if (op->code == RDS_WRITE_SET)
    return s->imm[op->imm + 2] != 1 || !index_total(s, a[2], s->imm[op->imm]);
  if (op->code == RDS_READ)
    return !index_total(s, a[0], s->mems[s->imm[op->imm]].depth);
  return op->code == RDS_ONEHOT || op->code == RDS_ONEHOT_VIEW;
}
/* Return only an existing exact query. A downstream payload preview in that
 * query remains an explicit dependency and can prevent it becoming a guard. */
static uint32_t object_query(const rds_sim *s, uint32_t object, uint32_t query) {
  for (uint32_t i = 0; i < s->no; ++i) {
    const rds_op *op = &s->ops[i];
    if (op->code == RDS_OBJECT_QUERY && s->imm[op->imm] == object &&
        s->imm[op->imm + 1] == query)
      return op->out;
  }
  return RDS_NONE;
}
int rds_semantic_regions(rds_sim *s, uint32_t *order, uint32_t count,
                         const uint32_t *producer, const uint32_t *owner,
                         bool *pinned, uint32_t *guards) {
  if (s->nv > (RDS_GUARD_OR - 3) / 2)
    return -1;
  uint32_t *demand = malloc((s->nv ? s->nv : 1) * sizeof *demand);
  uint32_t *position = malloc((s->no ? s->no : 1) * sizeof *position);
  uint32_t *result = malloc((count ? count : 1) * sizeof *result);
  bool *done = calloc(s->no ? s->no : 1, sizeof *done);
  size_t scalar_atoms = 2 * (size_t)s->nv + 2;
  size_t atoms = scalar_atoms + (s->schedule->guard_unions ? RDS_GUARD_OR_LIMIT : 0);
  bool *banned = calloc(atoms, sizeof *banned);
  uint32_t *sizes = calloc(atoms, sizeof *sizes);
  if (!demand || !position || !result || !done || !banned || !sizes) {
    free(demand);
    free(position);
    free(result);
    free(done);
    free(banned);
    free(sizes);
    return -1;
  }
  memset(position, 255, (size_t)s->no * sizeof *position);
  /* A flow's admission control is often emitted after its payload expression.
   * Pull the exact dependency cones of admission controls ahead of payloads
   * before deriving demand. Marking ancestors preserves topology, including
   * payload previews that really do influence readiness or validity. */
  memset(demand, 0, (size_t)s->nv * sizeof *demand);
  for (uint32_t i = 0; i < s->nx; ++i) {
    const rds_object *o = &s->objects[i];
    if (((o->kind == 1 && !(o->flags & 5)) || o->kind == 2 ||
         o->kind == 3 || o->kind == 9) && o->ni > 1)
      demand[o->inputs[1]] = 1;
  }
  for (uint32_t i = 0; i < s->nw; ++i)
    demand[s->writes[i].enable] = 1;
  for (uint32_t i = 0; i < s->nr; ++i) {
    uint32_t p = producer[s->regs[i].d];
    if (p != RDS_NONE && s->ops[p].code == RDS_MUX)
      demand[s->args[s->ops[p].args]] = 1;
  }
  for (uint32_t k = count; k--;) {
    const rds_op *op = &s->ops[order[k]];
    if (demand[op->out])
      for (uint32_t j = 0; j < op->nargs; ++j)
        demand[s->args[op->args + j]] = 1;
  }
  uint32_t ordered = 0;
  for (uint32_t pass = 0; pass < 2; ++pass)
    for (uint32_t k = 0; k < count; ++k)
      if (!!demand[s->ops[order[k]].out] == (pass == 0))
        result[ordered++] = order[k];
  memcpy(order, result, (size_t)count * sizeof *order);
  for (uint32_t i = 0; i < count; ++i)
    position[order[i]] = i;
  bool pruning;
  do {
    memset(demand, 255, (size_t)s->nv * sizeof *demand);
    memset(sizes, 0, atoms * sizeof *sizes);
    for (uint32_t i = 0; i < s->np; ++i)
      need(s, producer, demand, s->ports[i].value, 0);
    for (uint32_t i = 0; i < s->nr; ++i) {
      const rds_reg *r = &s->regs[i];
      need(s, producer, demand, r->d, 0);
      need(s, producer, demand, r->reset, 0);
      need(s, producer, demand, r->reset_value, 0);
    }
    for (uint32_t i = 0; i < s->nw; ++i) {
      const rds_write *w = &s->writes[i];
      need(s, producer, demand, w->data, atom(w->enable, true));
      need(s, producer, demand, w->address, atom(w->enable, true));
      need(s, producer, demand, w->enable, 0);
      need(s, producer, demand, w->mask, atom(w->enable, true));
    }
    for (uint32_t i = 0; i < s->ns; ++i) {
      const rds_read *r = &s->reads[i];
      need(s, producer, demand, r->address, 0);
      need(s, producer, demand, r->enable, 0);
      need(s, producer, demand, r->write, 0);
    }
    for (uint32_t i = 0; i < s->nc; ++i) {
      const rds_assert *a = &s->checks[i];
      need(s, producer, demand, a->condition, 0);
      need(s, producer, demand, a->reset, 0);
      need(s, producer, demand, a->guard, 0);
    }
    for (uint32_t i = 0; i < s->nx; ++i) {
      const rds_object *o = &s->objects[i];
      bool payload = (o->kind == 1 && !(o->flags & 5)) || o->kind == 2 ||
                     o->kind == 3 || o->kind == 9;
      uint32_t admission = RDS_NONE, occupied = RDS_NONE;
      if ((o->kind == 1 && !(o->flags & 4)) || o->kind == 2)
        admission = object_query(s, i, 1);
      else if (o->kind == 9)
        admission = object_query(s, i, 0);
      if (o->kind == 1 && !(o->flags & 4))
        occupied = object_query(s, i, 2);
      for (uint32_t j = 0; j < o->ni; ++j) {
        uint32_t guard = payload && j == 2 ? atom(o->inputs[1], true) : 0;
        if (j == 1 && admission != RDS_NONE)
          guard = atom(admission, true);
        if (j == 3 && occupied != RDS_NONE)
          guard = atom(occupied, true);
        if (j >= 3 && o->kind == 9) {
          uint32_t pending = object_query(s, i, j - 1);
          if (pending != RDS_NONE)
            guard = atom(pending, true);
        }
        need(s, producer, demand, o->inputs[j], guard);
      }
    }
    for (uint32_t k = count; k--;) {
      uint32_t id = order[k];
      const rds_op *op = &s->ops[id];
      const uint32_t *a = s->args + op->args;
      uint32_t g = demand[op->out];
      if (g == RDS_NONE || (partial(s, op) && !(s->guarded_onehot && op->code == RDS_ONEHOT)))
        g = 0;
      size_t slot = g & RDS_GUARD_OR ? scalar_atoms + (g & ~RDS_GUARD_OR) : g;
      if (g && banned[slot])
        g = 0;
      if (g) {
        uint32_t terms;
        const uint32_t *a = rds_guard_atoms(s->schedule, &g, &terms);
        bool valid = true;
        for (uint32_t t = 0; t < terms; ++t) {
          uint32_t p = producer[(a[t] - 2) / 2];
          if (p != RDS_NONE && (position[p] >= k || owner[p] != owner[id])) valid = false;
        }
        if (valid) for (uint32_t t = 0; t < terms; ++t)
          need(s, producer, demand, (a[t] - 2) / 2, 0);
        else g = 0;
      }
      guards[id] = g;
      if (g)
        sizes[slot] += s->values[op->out].words +
                    (s->guarded_onehot && op->code == RDS_ONEHOT ? 16u : 0u);
      if (!g && op->code == RDS_MUX && op->nargs == 3 &&
          s->values[a[0]].width == 1 && s->imm[op->imm] <= 1) {
        bool key = s->imm[op->imm] != 0;
        need(s, producer, demand, a[0], 0);
        need(s, producer, demand, a[1], atom(a[0], !key));
        need(s, producer, demand, a[2], atom(a[0], key));
      } else if (!g && (op->code == RDS_AND || op->code == RDS_OR) &&
                 s->values[op->out].width == 1) {
        uint32_t left = a[0], right = a[1];
        uint32_t lp =
            producer[left] == RDS_NONE ? 0 : position[producer[left]] + 1;
        uint32_t rp =
            producer[right] == RDS_NONE ? 0 : position[producer[right]] + 1;
        if (lp > rp) {
          uint32_t tmp = left;
          left = right;
          right = tmp;
        }
        need(s, producer, demand, left, 0);
        need(s, producer, demand, right, atom(left, op->code == RDS_AND));
      } else
        for (uint32_t j = 0; j < op->nargs; ++j)
          need(s, producer, demand, a[j], g);
    }
    /* Tiny conditional fragments cost a branch and break local forwarding.
     * One-hot selection has validation, address selection and payload traffic;
     * its optional demand policy charges at least a region's minimum cost.
     * Remove cheaper groups and propagate their newly eager demand. */
    pruning = false;
    for (size_t g = 2; g < atoms; ++g)
      if (sizes[g] && sizes[g] < 16) {
        banned[g] = true;
        pruning = true;
      }
  } while (pruning);
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t terms;
    const uint32_t *a = rds_guard_atoms(s->schedule, &guards[order[i]], &terms);
    for (uint32_t t = 0; t < terms; ++t) pinned[(a[t] - 2) / 2] = true;
  }
  /* Keep an event's ready work contiguous. This queue is constructed once;
   * execution contains only direct calls and ordinary conditional branches. */
  uint32_t previous = 0;
  for (uint32_t k = 0; k < count; ++k) {
    uint32_t choice = RDS_NONE;
    for (uint32_t j = 0; j < count; ++j) {
      uint32_t id = order[j];
      if (done[id])
        continue;
      const rds_op *op = &s->ops[id];
      bool ready = true;
      for (uint32_t a = 0; a < op->nargs; ++a) {
        uint32_t p = producer[s->args[op->args + a]];
        if (p != RDS_NONE && !done[p]) {
          ready = false;
          break;
        }
      }
      if (ready && guards[id]) {
        uint32_t terms;
        const uint32_t *a = rds_guard_atoms(s->schedule, &guards[id], &terms);
        for (uint32_t t = 0; t < terms; ++t) {
          uint32_t p = producer[(a[t] - 2) / 2];
          if (p != RDS_NONE && !done[p]) ready = false;
        }
      }
      if (!ready)
        continue;
      if (choice == RDS_NONE)
        choice = id;
      if (guards[id] == previous && owner[id] == owner[choice]) {
        choice = id;
        break;
      }
    }
    if (choice == RDS_NONE) {
      free(demand);
      free(position);
      free(result);
      free(done);
      free(banned);
      free(sizes);
      return -1;
    }
    result[k] = choice;
    done[choice] = true;
    previous = guards[choice];
  }
  memcpy(order, result, (size_t)count * sizeof *order);
  free(demand);
  free(position);
  free(result);
  free(done);
  free(banned);
  free(sizes);
  return 0;
}

/* Buffer storage changes at known publication events. Recover pure cones over
 * one buffer or register and constants. Ambient inputs, other state domains,
 * memory reads, arbitration, and partial operations end a stability domain. */
int rds_stable_payloads(rds_sim *s, uint32_t *order, uint32_t count,
                        bool *pinned, uint32_t *cache, bool *objects, bool *fields) {
  uint32_t domains=s->nx+(fields?s->nr:0);
  uint32_t *domain = malloc((s->nv ? s->nv : 1) * sizeof *domain);
  uint32_t *sizes = calloc(domains ? domains : 1, sizeof *sizes);
  uint32_t *result = malloc((count ? count : 1) * sizeof *result);
  if (!domain || !sizes || !result) {
    free(domain);
    free(sizes);
    free(result);
    return -1;
  }
  memset(domain, 255, (size_t)s->nv * sizeof *domain);
  for (uint32_t i = 0; i < count; ++i)
    cache[order[i]] = RDS_NONE;
  for (uint32_t i = 0; i < s->no; ++i)
    if (s->ops[i].code == RDS_CONST)
      domain[s->ops[i].out] = RDS_NONE - 1;
  if(fields)for(uint32_t i=0;i<s->nr;++i)domain[s->regs[i].q]=s->nx+i;
  for (uint32_t k = 0; k < count; ++k) {
    uint32_t id = order[k];
    const rds_op *op = &s->ops[id];
    uint32_t d = RDS_NONE;
    if (op->code == RDS_OBJECT_QUERY && !op->nargs) {
      uint32_t x = (uint32_t)s->imm[op->imm], q = (uint32_t)s->imm[op->imm + 1];
      const rds_object *o = &s->objects[x];
      if (!(o->flags & 1) && ((o->kind == 1 && !(o->flags & 4)) ||
                              ((o->kind == 2 || o->kind == 3) && q >= 2) ||
                              (o->kind == 9 && q >= 1)))
        d = x;
    } else if (op->code != RDS_OBJECT_QUERY && op->code != RDS_READ &&
               !partial(s, op)) {
      d = RDS_NONE - 1;
      for (uint32_t a = 0; a < op->nargs; ++a) {
        uint32_t other = domain[s->args[op->args + a]];
        if (other == RDS_NONE - 1)
          continue;
        if (other == RDS_NONE || (d != RDS_NONE - 1 && d != other)) {
          d = RDS_NONE;
          break;
        }
        d = other;
      }
    }
    // Only literal constants are initialized before the schedule. An
    // unfused constant-only computation still needs its ordinary producer.
    if (d == RDS_NONE - 1)
      d = RDS_NONE;
    domain[op->out] = d;
    if (d < domains) {
      cache[id] = d;
      sizes[d] += s->values[op->out].words;
    }
  }
  for (uint32_t k = 0; k < count; ++k) {
    uint32_t id = order[k], d = cache[id];
    if (d < domains && sizes[d] < 16)
      cache[id] = RDS_NONE;
    else if (d < domains)
      (d<s->nx?objects:fields)[d<s->nx?d:d-s->nx] = true;
  }
  // Only values escaping their cache group need persistent storage. Existing
  // output/effect pins already cover API ports and object/register consumers.
  for (uint32_t k = 0; k < count; ++k) {
    const rds_op *op = &s->ops[order[k]];
    for (uint32_t a = 0; a < op->nargs; ++a) {
      uint32_t v = s->args[op->args + a], d = domain[v];
      if (d < domains && (d<s->nx?objects[d]:fields[d-s->nx]) && cache[order[k]] != d)
        pinned[v] = true;
    }
  }
  if(fields)for(uint32_t k=0;k<count;++k){
    const rds_op *op=&s->ops[order[k]];
    uint32_t d=cache[order[k]];
    if(d<s->nx||d>=domains)continue;
    const rds_reg *r=&s->regs[d-s->nx];
    for(uint32_t a=0;a<op->nargs;++a)if(s->args[op->args+a]==r->q){
      uint32_t low=0,width=s->values[r->q].width;
      if(op->code==RDS_SLICE){low=(uint32_t)s->imm[op->imm];width=s->values[op->out].width;}
      for(uint32_t bit=low;bit<low+width;++bit)
        s->schedule->field_masks[r->next+bit/64]|=UINT64_C(1)<<(bit%64);
    }
  }
  uint32_t n = 0;
  for (uint32_t x = 0; x < domains; ++x)
    if (x<s->nx?objects[x]:fields[x-s->nx])
      for (uint32_t k = 0; k < count; ++k)
        if (cache[order[k]] == x)
          result[n++] = order[k];
  for (uint32_t k = 0; k < count; ++k)
    if (cache[order[k]] == RDS_NONE)
      result[n++] = order[k];
  memcpy(order, result, (size_t)count * sizeof *order);
  free(domain);
  free(sizes);
  free(result);
  return 0;
}

/* One large closed pure cone may reuse several current FIFO/matcher snapshots.
 * All inputs are constants or members of the selected cone, so moving it ahead
 * of ordinary work preserves topology. Effects and current-state publication
 * never belong to the cone. Register, memory, host and ambient inputs reject it. */
int rds_flow_cache(rds_sim *s, uint32_t *order, uint32_t count,
                   bool *pinned, uint32_t *cache, const uint32_t *guards) {
  enum { WORDS = 4, MAX_OBJECTS = WORDS * 64 };
  typedef struct { uint64_t bits[WORDS]; bool valid; } roots;
  if (s->nx > MAX_OBJECTS || s->schedule->count != 1) return 0;
  if ((uint64_t)s->nv * (sizeof(roots) + sizeof(bool)) +
      (uint64_t)count * sizeof(uint32_t) > UINT64_C(64) * 1024 * 1024)
    return 0;
  roots *sources = calloc(s->nv ? s->nv : 1, sizeof *sources);
  bool *selected = calloc(s->nv ? s->nv : 1, sizeof *selected);
  uint32_t *result = malloc((count ? count : 1) * sizeof *result);
  if (!sources || !selected || !result) goto fail;
  for (uint32_t i=0;i<s->no;++i)
    if (s->ops[i].code==RDS_CONST) sources[s->ops[i].out].valid=true;
  for (uint32_t k=0;k<count;++k) {
    const rds_op *o=&s->ops[order[k]]; roots r={.valid=true};
    if (partial(s,o) || o->code==RDS_READ) r.valid=false;
    for (uint32_t a=0;a<o->nargs;++a) {
      const roots *v=&sources[s->args[o->args+a]];
      r.valid &= v->valid;
      for (unsigned w=0;w<WORDS;++w) r.bits[w]|=v->bits[w];
    }
    if (o->code==RDS_OBJECT_QUERY) {
      uint32_t id=s->imm[o->imm];const rds_object *object=&s->objects[id];
      r.valid &= (object->kind==1 && object->depth==1 && object->flags==0) ||
                 (object->kind==10 && object->flags==32);
      r.bits[id/64]|=UINT64_C(1)<<(id%64);
    }
    sources[o->out]=r;
  }
  uint32_t best=0,chosen_matcher=RDS_NONE; roots chosen={0};
  for (uint32_t matcher=0;matcher<s->nx;++matcher) {
    if (s->objects[matcher].kind!=10 || s->objects[matcher].flags!=32) continue;
    roots owners={.valid=true};uint32_t total=0,eager=0;
    for (uint32_t k=0;k<count;++k) {
      const roots *r=&sources[s->ops[order[k]].out];
      if (!r->valid || !(r->bits[matcher/64]&(UINT64_C(1)<<(matcher%64)))) continue;
      bool other=false;
      for (uint32_t x=0;x<s->nx;++x)
        if (x!=matcher && s->objects[x].kind==10 &&
            (r->bits[x/64]&(UINT64_C(1)<<(x%64)))) other=true;
      if (other) continue;
      ++total;
      if (cache[order[k]]==RDS_NONE && (!guards || !guards[order[k]])) ++eager;
      for (unsigned w=0;w<WORDS;++w) owners.bits[w]|=r->bits[w];
    }
    unsigned keys=0;for(unsigned w=0;w<WORDS;++w)keys+=__builtin_popcountll(owners.bits[w]);
    if (total>=128 && eager>=32 && keys<=64 && eager>best) {
      best=eager;chosen=owners;chosen_matcher=matcher;
    }
  }
  if (!best) { free(sources);free(selected);free(result);return 0; }
  rds_schedule *p=s->schedule;
  p->flow_cache_objects=calloc(s->nx?s->nx:1,sizeof *p->flow_cache_objects);
  if (!p->flow_cache_objects) goto fail;
  for (uint32_t id=0;id<s->nx;++id)
    if (chosen.bits[id/64]&(UINT64_C(1)<<(id%64))) {
      p->flow_cache_objects[id]=true;p->cache_objects[id]=true;p->cache_lanes[id]=true;
    }
  for (uint32_t k=0;k<count;++k) {
    const rds_op *o=&s->ops[order[k]];const roots *r=&sources[o->out];
    bool included=r->valid &&
        (r->bits[chosen_matcher/64]&(UINT64_C(1)<<(chosen_matcher%64)));
    for (unsigned w=0;w<WORDS;++w) included &= !(r->bits[w]&~chosen.bits[w]);
    if (included) { selected[o->out]=true;cache[order[k]]=s->nx+s->nr; }
  }
  /* Pure derived constants are not initialized sources. Include any needed
   * zero-owner producers rather than moving consumers ahead of their work. */
  for (uint32_t k=count;k--;) {
    const rds_op *o=&s->ops[order[k]];
    if (!selected[o->out]) continue;
    cache[order[k]]=s->nx+s->nr;
    for (uint32_t a=0;a<o->nargs;++a) selected[s->args[o->args+a]]=true;
  }
  for (uint32_t k=0;k<count;++k) {
    const rds_op *o=&s->ops[order[k]];
    if (!selected[o->out])
      for (uint32_t a=0;a<o->nargs;++a)
        if (selected[s->args[o->args+a]]) pinned[s->args[o->args+a]]=true;
  }
  uint32_t n=0;
  for (unsigned pass=0;pass<2;++pass)
    for (uint32_t k=0;k<count;++k)
      if (selected[s->ops[order[k]].out]==(pass==0)) result[n++]=order[k];
  memcpy(order,result,(size_t)count*sizeof *order);
  free(sources);free(selected);free(result);return 0;
fail:
  free(sources);free(selected);free(result);return -1;
}
