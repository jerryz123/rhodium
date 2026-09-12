/* Shares exact Sv39 TLB lookup and permission semantics with generated C. */
#ifndef RDS_TLB_H
#define RDS_TLB_H
/* Entry word zero contains PPN[52:9], level[8:7], U/R/W/X/G/A/D[6:0],
 * and valid[53]. Word one contains VPN[26:0]. Lookup word zero contains
 * physical address[55:0], hit[56], and Bare bypass[57]; word one is metadata.
 * Cache-management access requires A and load-or-store permission, not D. */
#define RDS_TLB_CODE \
typedef struct { uint64_t low, metadata; } rds_tlb_result; \
static inline rds_tlb_result rds_tlb_lookup(const uint64_t *entries,unsigned depth,uint64_t address,bool enabled) { \
    const uint64_t address_mask=UINT64_C(0x00ffffffffffffff); \
    if(!enabled)return (rds_tlb_result){(address&address_mask)|(UINT64_C(3)<<56),0}; \
    uint64_t vpn=(address>>12)&UINT64_C(0x7ffffff),metadata=entries[0];bool found=false; \
    for(unsigned i=0;i<depth;++i){uint64_t entry=entries[2*i];if(!(entry&(UINT64_C(1)<<53)))continue; \
        unsigned level=(unsigned)(entry>>7)&3,shift=level==1?9:level==2?18:0; \
        if((entries[2*i+1]>>shift)==(vpn>>shift)){metadata=entry;found=true;break;}} \
    unsigned level=(unsigned)(metadata>>7)&3,low_bits=level==0?12:level==1?21:30; \
    uint64_t page_mask=(UINT64_C(1)<<low_bits)-1,ppn=(metadata>>9)&UINT64_C(0xfffffffffff); \
    uint64_t physical=((ppn<<12)&~page_mask)|(address&page_mask); \
    bool canonical=(address>>39)==((address&(UINT64_C(1)<<38))?UINT64_C(0x1ffffff):0); \
    return (rds_tlb_result){(physical&address_mask)|((uint64_t)(found&&canonical)<<56),metadata}; \
} \
static inline uint64_t rds_tlb_fault(rds_tlb_result lookup,uint64_t access,uint64_t privilege,bool sum,bool mxr,bool probe) { \
    if(!(lookup.low&(UINT64_C(1)<<56))||(lookup.low&(UINT64_C(1)<<57)))return 0; \
    uint64_t p=lookup.metadata;bool user=(p>>6)&1,read=(p>>5)&1,write=(p>>4)&1,execute=(p>>3)&1; \
    bool u=privilege==0,s=privilege==1,allowed; \
    if(probe){bool fetch_priv=u?user:!user,data_priv=u?user:!user||(s&&sum); \
        allowed=(fetch_priv&&execute)||(data_priv&&(read||(mxr&&execute)||write));} \
    else{bool priv=u?user:!user||(s&&sum&&access!=0); \
        bool permission=access==0?execute:access==1?read||(mxr&&execute):access==3?read||write||(mxr&&execute):write&&(p&1); \
        allowed=priv&&permission&&((p>>1)&1);} \
    return !allowed; \
}
#endif
