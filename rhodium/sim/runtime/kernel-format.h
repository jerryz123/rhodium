/* Defines bounded, pointer-free pure contract programs shared by both compilers. */
#ifndef RDS_KERNEL_FORMAT_H
#define RDS_KERNEL_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#define RDS_KERNEL_OPCODE 32
#define RDS_KERNEL_INPUTS 128
#define RDS_KERNEL_OPS 512
#define RDS_KERNEL_VALUES (RDS_KERNEL_INPUTS + RDS_KERNEL_OPS)
#define RDS_KERNEL_ARGS 8192
#define RDS_KERNEL_WORDS 4096
struct rds_kernel_instruction { uint32_t code, out, args, nargs, imm, nimm; };
struct rds_kernel_format {
    uint32_t inputs, count, values, arguments, words;
    uint32_t widths[RDS_KERNEL_VALUES], offsets[RDS_KERNEL_VALUES];
    struct rds_kernel_instruction ops[RDS_KERNEL_OPS];
    uint32_t args[RDS_KERNEL_ARGS];
};
/* Widths precede records; each record is code, argument count, immediate count,
 * local argument IDs, then ordinary opcode immediates. Outputs are sequential.
 * Only total operations are legal. Bounds-dependent and state operations remain
 * outside the program, including one-hot selection with its debug checks. */
static inline int rds_kernel_parse(const uint64_t *im, size_t size, struct rds_kernel_format *k) {
    if(size<3 || im[0]<1 || im[0]>3 || im[1]>RDS_KERNEL_INPUTS || !im[2] || im[2]>RDS_KERNEL_OPS)return 0;
    k->inputs=(uint32_t)im[1];k->count=(uint32_t)im[2];k->values=k->inputs+k->count;
    k->arguments=0;k->words=0;
    size_t at=3;
    if(k->values>size-at)return 0;
    for(uint32_t i=0;i<k->values;++i){
        uint64_t w=im[at++];if(!w || w>RDS_KERNEL_WORDS*64)return 0;
        k->widths[i]=(uint32_t)w;k->offsets[i]=k->words;k->words+=(uint32_t)((w+63)/64);
        if(k->words>RDS_KERNEL_WORDS)return 0;
    }
    for(uint32_t i=0;i<k->count;++i){
        if(size-at<3)return 0;
        uint64_t code=im[at++],na=im[at++],ni=im[at++];
        if(code>31 || code==16 || (code>=21 && code<=24) || code==29 ||
           na>RDS_KERNEL_ARGS-k->arguments || na>size-at || ni>size-at-na || at+na>UINT32_MAX || ni>UINT32_MAX)return 0;
        struct rds_kernel_instruction *o=&k->ops[i];
        o->code=(uint32_t)code;o->out=k->inputs+i;o->args=k->arguments;o->nargs=(uint32_t)na;
        for(uint32_t j=0;j<na;++j){if(im[at]>=o->out)return 0;k->args[k->arguments++]=(uint32_t)im[at++];}
        o->imm=(uint32_t)at;o->nimm=(uint32_t)ni;at+=(size_t)ni;
    }
    return at==size;
}
#endif
