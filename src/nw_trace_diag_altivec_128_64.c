/**
 * @file
 *
 * @author jeffrey.daily@gmail.com
 *
 * Copyright (c) 2015 Battelle Memorial Institute.
 */
#include "config.h"

#include <stdlib.h>



#include "parasail.h"
#include "parasail/memory.h"
#include "parasail/internal_altivec.h"

#define NEG_INF (INT64_MIN/(int64_t)(2))


static inline void arr_store_si128(
        int8_t *array,
        vec128i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (0 <= i+0 && i+0 < s1Len && 0 <= j-0 && j-0 < s2Len) {
        array[1LL*(i+0)*s2Len + (j-0)] = (int8_t)_mm_extract_epi64(vWH, 1);
    }
    if (0 <= i+1 && i+1 < s1Len && 0 <= j-1 && j-1 < s2Len) {
        array[1LL*(i+1)*s2Len + (j-1)] = (int8_t)_mm_extract_epi64(vWH, 0);
    }
}

#define FNAME parasail_nw_trace_diag_altivec_128_64

parasail_result_t* FNAME(
        const char * const restrict _s1, const int s1Len,
        const char * const restrict _s2, const int s2Len,
        const int open, const int gap, const parasail_matrix_t *matrix)
{
    /* declare local variables */
    int32_t N = 0;
    int32_t PAD = 0;
    int32_t PAD2 = 0;
    int32_t s1Len_PAD = 0;
    int32_t s2Len_PAD = 0;
    int64_t * restrict s1 = NULL;
    int64_t * restrict s2B = NULL;
    int64_t * restrict _H_pr = NULL;
    int64_t * restrict _F_pr = NULL;
    int64_t * restrict s2 = NULL;
    int64_t * restrict H_pr = NULL;
    int64_t * restrict F_pr = NULL;
    parasail_result_t *result = NULL;
    int32_t i = 0;
    int32_t j = 0;
    int32_t end_query = 0;
    int32_t end_ref = 0;
    int64_t score = 0;
    vec128i vNegInf;
    vec128i vOpen;
    vec128i vGap;
    vec128i vOne;
    vec128i vN;
    vec128i vGapN;
    vec128i vNegOne;
    vec128i vI;
    vec128i vJreset;
    vec128i vMax;
    vec128i vILimit;
    vec128i vILimit1;
    vec128i vJLimit;
    vec128i vJLimit1;
    vec128i vIBoundary;
    vec128i vTDiag;
    vec128i vTIns;
    vec128i vTDel;
    vec128i vTDiagE;
    vec128i vTInsE;
    vec128i vTDiagF;
    vec128i vTDelF;
    

    /* validate inputs */
    PARASAIL_CHECK_NULL(_s1);
    PARASAIL_CHECK_GT0(s1Len);
    PARASAIL_CHECK_NULL(_s2);
    PARASAIL_CHECK_GT0(s2Len);
    PARASAIL_CHECK_GE0(open);
    PARASAIL_CHECK_GE0(gap);
    PARASAIL_CHECK_NULL(matrix);

    /* initialize stack variables */
    N = 2; /* number of values in vector */
    PAD = N-1;
    PAD2 = PAD*2;
    s1Len_PAD = s1Len+PAD;
    s2Len_PAD = s2Len+PAD;
    i = 0;
    j = 0;
    end_query = s1Len-1;
    end_ref = s2Len-1;
    score = NEG_INF;
    vNegInf = _mm_set1_epi64(NEG_INF);
    vOpen = _mm_set1_epi64(open);
    vGap  = _mm_set1_epi64(gap);
    vOne = _mm_set1_epi64(1);
    vN = _mm_set1_epi64(N);
    vGapN = _mm_set1_epi64(gap*N);
    vNegOne = _mm_set1_epi64(-1);
    vI = _mm_set_epi64(0,1);
    vJreset = _mm_set_epi64(0,-1);
    vMax = vNegInf;
    vILimit = _mm_set1_epi64(s1Len);
    vILimit1 = _mm_sub_epi64(vILimit, vOne);
    vJLimit = _mm_set1_epi64(s2Len);
    vJLimit1 = _mm_sub_epi64(vJLimit, vOne);
    vIBoundary = _mm_set_epi64(
            -open-0*gap,
            -open-1*gap
            );
    vTDiag = _mm_set1_epi64(PARASAIL_DIAG);
    vTIns = _mm_set1_epi64(PARASAIL_INS);
    vTDel = _mm_set1_epi64(PARASAIL_DEL);
    vTDiagE = _mm_set1_epi64(PARASAIL_DIAG_E);
    vTInsE = _mm_set1_epi64(PARASAIL_INS_E);
    vTDiagF = _mm_set1_epi64(PARASAIL_DIAG_F);
    vTDelF = _mm_set1_epi64(PARASAIL_DEL_F);
    

    /* initialize result */
    result = parasail_result_new_trace(s1Len, s2Len, 16, sizeof(int8_t));
    if (!result) return NULL;

    /* set known flags */
    result->flag |= PARASAIL_FLAG_NW | PARASAIL_FLAG_DIAG
        | PARASAIL_FLAG_TRACE
        | PARASAIL_FLAG_BITS_64 | PARASAIL_FLAG_LANES_2;

    /* initialize heap variables */
    s1 = parasail_memalign_int64_t(16, s1Len+PAD);
    s2B= parasail_memalign_int64_t(16, s2Len+PAD2);
    _H_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    _F_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    s2 = s2B+PAD; /* will allow later for negative indices */
    H_pr = _H_pr+PAD;
    F_pr = _F_pr+PAD;

    /* validate heap variables */
    if (!s1) return NULL;
    if (!s2B) return NULL;
    if (!_H_pr) return NULL;
    if (!_F_pr) return NULL;

    /* convert _s1 from char to int in range 0-23 */
    for (i=0; i<s1Len; ++i) {
        s1[i] = matrix->mapper[(unsigned char)_s1[i]];
    }
    /* pad back of s1 with dummy values */
    for (i=s1Len; i<s1Len_PAD; ++i) {
        s1[i] = 0; /* point to first matrix row because we don't care */
    }

    /* convert _s2 from char to int in range 0-23 */
    for (j=0; j<s2Len; ++j) {
        s2[j] = matrix->mapper[(unsigned char)_s2[j]];
    }
    /* pad front of s2 with dummy values */
    for (j=-PAD; j<0; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }
    /* pad back of s2 with dummy values */
    for (j=s2Len; j<s2Len_PAD; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }

    /* set initial values for stored row */
    for (j=0; j<s2Len; ++j) {
        H_pr[j] = -open - j*gap;
        F_pr[j] = NEG_INF;
    }
    /* pad front of stored row values */
    for (j=-PAD; j<0; ++j) {
        H_pr[j] = NEG_INF;
        F_pr[j] = NEG_INF;
    }
    /* pad back of stored row values */
    for (j=s2Len; j<s2Len+PAD; ++j) {
        H_pr[j] = NEG_INF;
        F_pr[j] = NEG_INF;
    }
    H_pr[-1] = 0; /* upper left corner */

    /* iterate over query sequence */
    for (i=0; i<s1Len; i+=N) {
        vec128i vNH = vNegInf;
        vec128i vWH = vNegInf;
        vec128i vE = vNegInf;
        vec128i vE_opn = vNegInf;
        vec128i vE_ext = vNegInf;
        vec128i vF = vNegInf;
        vec128i vF_opn = vNegInf;
        vec128i vF_ext = vNegInf;
        vec128i vJ = vJreset;
        const int * const restrict matrow0 = &matrix->matrix[matrix->size*s1[i+0]];
        const int * const restrict matrow1 = &matrix->matrix[matrix->size*s1[i+1]];
        vNH = _mm_srli_si128(vNH, 8);
        vNH = _mm_insert_epi64(vNH, H_pr[-1], 1);
        vWH = _mm_srli_si128(vWH, 8);
        vWH = _mm_insert_epi64(vWH, -open - i*gap, 1);
        H_pr[-1] = -open - (i+N)*gap;
        /* iterate over database sequence */
        for (j=0; j<s2Len+PAD; ++j) {
            vec128i vMat;
            vec128i vNWH = vNH;
            vNH = _mm_srli_si128(vWH, 8);
            vNH = _mm_insert_epi64(vNH, H_pr[j], 1);
            vF = _mm_srli_si128(vF, 8);
            vF = _mm_insert_epi64(vF, F_pr[j], 1);
            vF_opn = _mm_sub_epi64(vNH, vOpen);
            vF_ext = _mm_sub_epi64(vF, vGap);
            vF = _mm_max_epi64(vF_opn, vF_ext);
            vE_opn = _mm_sub_epi64(vWH, vOpen);
            vE_ext = _mm_sub_epi64(vE, vGap);
            vE = _mm_max_epi64(vE_opn, vE_ext);
            vMat = _mm_set_epi64(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]]
                    );
            vNWH = _mm_add_epi64(vNWH, vMat);
            vWH = _mm_max_epi64(vNWH, vE);
            vWH = _mm_max_epi64(vWH, vF);
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                vec128i cond = _mm_cmpeq_epi64(vJ,vNegOne);
                vWH = _mm_blendv_epi8(vWH, vIBoundary, cond);
                vF = _mm_blendv_epi8(vF, vNegInf, cond);
                vE = _mm_blendv_epi8(vE, vNegInf, cond);
            }
            
            /* trace table */
            {
                vec128i case1 = _mm_cmpeq_epi64(vWH, vNWH);
                vec128i case2 = _mm_cmpeq_epi64(vWH, vF);
                vec128i vT = _mm_blendv_epi8(
                        _mm_blendv_epi8(vTIns, vTDel, case2),
                        vTDiag,
                        case1);
                vec128i condE = _mm_cmpgt_epi64(vE_opn, vE_ext);
                vec128i condF = _mm_cmpgt_epi64(vF_opn, vF_ext);
                vec128i vET = _mm_blendv_epi8(vTInsE, vTDiagE, condE);
                vec128i vFT = _mm_blendv_epi8(vTDelF, vTDiagF, condF);
                vT = _mm_or_si128(vT, vET);
                vT = _mm_or_si128(vT, vFT);
                arr_store_si128(result->trace->trace_table, vT, i, s1Len, j, s2Len);
            }
            H_pr[j-1] = (int64_t)_mm_extract_epi64(vWH,0);
            F_pr[j-1] = (int64_t)_mm_extract_epi64(vF,0);
            /* as minor diagonal vector passes across table, extract
               last table value at the i,j bound */
            {
                vec128i cond_valid_I = _mm_cmpeq_epi64(vI, vILimit1);
                vec128i cond_valid_J = _mm_cmpeq_epi64(vJ, vJLimit1);
                vec128i cond_all = _mm_and_si128(cond_valid_I, cond_valid_J);
                vMax = _mm_blendv_epi8(vMax, vWH, cond_all);
            }
            vJ = _mm_add_epi64(vJ, vOne);
        }
        vI = _mm_add_epi64(vI, vN);
        vIBoundary = _mm_sub_epi64(vIBoundary, vGapN);
    }

    /* max in vMax */
    for (i=0; i<N; ++i) {
        int64_t value;
        value = (int64_t) _mm_extract_epi64(vMax, 1);
        if (value > score) {
            score = value;
        }
        vMax = _mm_slli_si128(vMax, 8);
    }

    

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;

    parasail_free(_F_pr);
    parasail_free(_H_pr);
    parasail_free(s2B);
    parasail_free(s1);

    return result;
}


