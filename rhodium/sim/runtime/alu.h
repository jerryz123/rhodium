/* Shares functional physical-control ALU semantics between reference and emitted C. */
#ifndef RDS_ALU_H
#define RDS_ALU_H
/* Reverse/shift/reverse in the RTL becomes a host left shift. Arithmetic-left
 * physical controls fill low bits from bit zero, preserving unusual controls. */
#define RDS_ALU_CODE \
static inline uint64_t rds_alu_mask(unsigned w) { return w==64?UINT64_MAX:UINT64_C(0xffffffff); } \
static inline uint64_t rds_alu_sx(uint64_t x,unsigned w) { uint64_t bit=UINT64_C(1)<<(w-1);return ((x&((bit-1)|bit))^bit)-bit; } \
static inline uint64_t rds_alu(unsigned w,uint64_t left,uint64_t right,uint64_t c) { \
    uint64_t mask=rds_alu_mask(w), result=0; \
    unsigned word=w==64&&(c&1), sel=(unsigned)(c>>20)&7; \
    left&=mask; right&=mask; \
    switch(sel) { \
    case 0: case 3: case 4: { \
        uint64_t a=((w==64&&(c&8))?left&UINT64_C(0xffffffff):left)<<((c>>1)&3); \
        unsigned sub=(unsigned)(c>>12)&1; \
        uint64_t sum=(a+(sub?~right:right)+sub)&mask; \
        if(sel==0) result=sum; \
        else { unsigned ls=(unsigned)(left>>(w-1)),rs=(unsigned)(right>>(w-1)); \
            unsigned less=ls==rs?(unsigned)(sum>>(w-1)):((c>>11)&1)?ls:rs; \
            result=sel==3?less:(less^((c>>10)&1))?left:right; } \
        break; \
    } \
    case 2: { \
        uint64_t b=right; \
        switch((c>>4)&3) { case 1:b=UINT64_C(1)<<(right&(w-1));break;case 2:b=right?mask:0;break;default:break; } \
        if(c&64)b=~b; \
        switch((c>>18)&3) {case 1:result=left|b;break;case 2:result=left^b;break;default:result=left&b;break;} \
        break; \
    } \
    case 1: case 6: { \
        unsigned arith=(unsigned)(c>>8)&1,shright=(unsigned)(c>>9)&1; \
        unsigned bits=word?32:w,amount=(unsigned)right&(bits-1); \
        uint64_t source=word?(arith?rds_alu_sx(left,32):left&UINT64_C(0xffffffff)):(w==64&&(c&8))?left&UINT64_C(0xffffffff):left; \
        source&=mask; \
        uint64_t shared=shright?source>>amount:source<<amount; \
        if(amount&&arith) { \
            if(shright&&(source>>(w-1)))shared|=mask&~(mask>>amount); \
            else if(!shright&&(source&1))shared|=(UINT64_C(1)<<amount)-1; \
        } \
        if(sel==1)result=(c&128)?shared&1:shared; \
        else { unsigned other=(0u-amount)&(bits-1); \
            result=shared|(shright?source<<other:source>>other); } \
        break; \
    } \
    case 5: { \
        unsigned bits=word?32:w,kind=(unsigned)(c>>16)&3; \
        uint64_t x=word?left&UINT64_C(0xffffffff):left; \
        result=kind==2?(unsigned)__builtin_popcountll(x):!x?bits:kind==0?(unsigned)__builtin_clzll(x)-(64-bits):(unsigned)__builtin_ctzll(x); \
        break; \
    } \
    case 7: \
        switch((c>>13)&7) { \
        case 0:for(unsigned i=0;i<w;i+=8)result|=((left>>i)&255?UINT64_C(255):0)<<i;break; \
        case 1:result=__builtin_bswap64(left);if(w==32)result>>=32;break; \
        case 2:result=rds_alu_sx(left,8);break; \
        case 3:result=rds_alu_sx(left,16);break; \
        case 4:result=left&65535;break; \
        default:break; \
        } \
        break; \
    } \
    return (word?rds_alu_sx(result,32):result)&mask; \
}
#endif
