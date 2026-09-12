/* Shares compile-time emitters for bound operations, objects, and state effects. */
#ifndef RDS_CODEGEN_H
#define RDS_CODEGEN_H
#include "schedule.h"
bool rds_c_memory_view(const rds_sim *,const rds_op *);
uint32_t rds_c_arena_word(const rds_sim *s, uint32_t word);
uint64_t rds_c_ref(const rds_sim *s, uint32_t id);
bool rds_c_small_word(const rds_sim *s, uint32_t word);
bool rds_c_small_value(const rds_sim *s, uint32_t id);
void rds_c_store_small(FILE *f, const rds_sim *s, uint32_t id);
void rds_c_pointer(FILE *f, uint64_t ref);
void rds_c_prelude(FILE *f, const rds_sim *s);
bool rds_c_decode_bits(const rds_sim *s, const rds_op *op, uint64_t *bits);
void rds_c_decode_tables(FILE *f, const rds_sim *s);
uint32_t rds_c_decode_table_id(const rds_sim *s, const rds_op *op);
void rds_c_update_next(FILE *f, const rds_sim *s, const rds_op *op, size_t next);
void rds_c_operation(FILE *f, const rds_sim *s, const rds_op *op);
bool rds_c_onehot_storage(const rds_sim *s, const rds_op *op);
void rds_c_object_layout(FILE *f, const rds_sim *s,const bool *pending);
void rds_c_object_ref(FILE *f, const rds_sim *s, uint32_t id);
void rds_c_object_view(char *out, size_t size, const rds_sim *s, uint32_t id, bool validity);
void rds_c_query(FILE *f, const rds_sim *s, const rds_op *op);
void rds_c_objects(FILE *f, const rds_sim *s, uint32_t lane, uint32_t phase, const bool *pending, const uint32_t *prepare_at);
void rds_c_prepare_object(FILE *f, const rds_sim *s, uint32_t id, const bool *pending);
void rds_c_transition_objects(FILE *f, const rds_sim *s, uint32_t lane, const bool *pending, const uint32_t *prepare_at);
void rds_c_prepare_fifo_payload(FILE *f, const rds_sim *s, uint32_t id);
bool rds_c_fifo_batchable(const rds_sim *s, uint32_t id);
void rds_c_prepare_fifo_batch(FILE *f, const rds_sim *s, uint32_t first, uint32_t count, const bool *pending);
void rds_c_effects(FILE *f, const rds_sim *s, uint32_t phase, const bool *staged);
void rds_c_bound_effects(FILE *f,const rds_sim *s);
void rds_c_registers(FILE *f, const rds_sim *s, uint32_t owner, const bool *staged);
uint32_t rds_c_offer_owner(const rds_sim *, uint32_t);
void rds_c_offer_layout(FILE *, const rds_sim *);
void rds_c_offers(FILE *, const rds_sim *);
#endif
