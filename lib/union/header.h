#ifndef __UNION_H__
#define __UNION_H__

#include "struct.h"

void union_num_display(union_num_t u);
void union_num_display_full(char tag[], union_num_t u);

union_num_t union_num_wrap_sig(sig_num_t sig, uint64_t size);
union_num_t union_num_wrap_flt(flt_num_t flt, uint64_t size);
flt_num_t union_num_unwrap_flt(union_num_t u);
union_num_t union_num_copy(union_num_t u);
void union_num_free(union_num_t u);

void file_write_union_num(file_p fp, union_num_t u);
union_num_t file_read_union_num(FILE *fp, uint64_t index);

union_num_t union_num_add(union_num_t u_1, union_num_t u_2);
union_num_t union_num_mul(union_num_t u_1, union_num_t u_2);

#endif
