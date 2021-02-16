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
#include "parasail/internal_neon.h"

#define NEG_INF (INT64_MIN/(int64_t)(2))


static inline void arr_store_si128(
        int8_t *array,
        simde__m128i vWH,
        int32_t i,
        int32_t s1Len,
        int32_t j,
        int32_t s2Len)
{
    if (0 <= i+0 && i+0 < s1Len && 0 <= j-0 && j-0 < s2Len) {
        array[1LL*(i+0)*s2Len + (j-0)] = (int8_t)simde_mm_extract_epi64(vWH, 1);
    }
    if (0 <= i+1 && i+1 < s1Len && 0 <= j-1 && j-1 < s2Len) {
        array[1LL*(i+1)*s2Len + (j-1)] = (int8_t)simde_mm_extract_epi64(vWH, 0);
    }
}

#define FNAME parasail_nw_trace_diag_neon_128_64

parasail_result_t* FNAME(
        const char * const restrict _s1, const int _s1Len,
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
    int32_t s1Len = 0;
    int32_t end_query = 0;
    int32_t end_ref = 0;
    int64_t score = 0;
    simde__m128i vNegInf;
    simde__m128i vOpen;
    simde__m128i vGap;
    simde__m128i vOne;
    simde__m128i vN;
    simde__m128i vGapN;
    simde__m128i vNegOne;
    simde__m128i vI;
    simde__m128i vJreset;
    simde__m128i vMax;
    simde__m128i vILimit;
    simde__m128i vILimit1;
    simde__m128i vJLimit;
    simde__m128i vJLimit1;
    simde__m128i vIBoundary;
    simde__m128i vTDiag;
    simde__m128i vTIns;
    simde__m128i vTDel;
    simde__m128i vTDiagE;
    simde__m128i vTInsE;
    simde__m128i vTDiagF;
    simde__m128i vTDelF;
    

    /* validate inputs */
    PARASAIL_CHECK_NULL(_s2);
    PARASAIL_CHECK_GT0(s2Len);
    PARASAIL_CHECK_GE0(open);
    PARASAIL_CHECK_GE0(gap);
    PARASAIL_CHECK_NULL(matrix);
    if (matrix->type == PARASAIL_MATRIX_TYPE_SQUARE) {
        PARASAIL_CHECK_NULL(_s1);
        PARASAIL_CHECK_GT0(_s1Len);
    }

    /* initialize stack variables */
    N = 2; /* number of values in vector */
    PAD = N-1;
    PAD2 = PAD*2;
    s1Len = matrix->type == PARASAIL_MATRIX_TYPE_SQUARE ? _s1Len : matrix->length;
    s1Len_PAD = s1Len+PAD;
    s2Len_PAD = s2Len+PAD;
    i = 0;
    j = 0;
    end_query = s1Len-1;
    end_ref = s2Len-1;
    score = NEG_INF;
    vNegInf = simde_mm_set1_epi64x(NEG_INF);
    vOpen = simde_mm_set1_epi64x(open);
    vGap  = simde_mm_set1_epi64x(gap);
    vOne = simde_mm_set1_epi64x(1);
    vN = simde_mm_set1_epi64x(N);
    vGapN = simde_mm_set1_epi64x(gap*N);
    vNegOne = simde_mm_set1_epi64x(-1);
    vI = simde_mm_set_epi64x(0,1);
    vJreset = simde_mm_set_epi64x(0,-1);
    vMax = vNegInf;
    vILimit = simde_mm_set1_epi64x(s1Len);
    vILimit1 = simde_mm_sub_epi64(vILimit, vOne);
    vJLimit = simde_mm_set1_epi64x(s2Len);
    vJLimit1 = simde_mm_sub_epi64(vJLimit, vOne);
    vIBoundary = simde_mm_set_epi64x(
            -open-0*gap,
            -open-1*gap
            );
    vTDiag = simde_mm_set1_epi64x(PARASAIL_DIAG);
    vTIns = simde_mm_set1_epi64x(PARASAIL_INS);
    vTDel = simde_mm_set1_epi64x(PARASAIL_DEL);
    vTDiagE = simde_mm_set1_epi64x(PARASAIL_DIAG_E);
    vTInsE = simde_mm_set1_epi64x(PARASAIL_INS_E);
    vTDiagF = simde_mm_set1_epi64x(PARASAIL_DIAG_F);
    vTDelF = simde_mm_set1_epi64x(PARASAIL_DEL_F);
    

    /* initialize result */
    result = parasail_result_new_trace(s1Len, s2Len, 16, sizeof(int8_t));
    if (!result) return NULL;

    /* set known flags */
    result->flag |= PARASAIL_FLAG_NW | PARASAIL_FLAG_DIAG
        | PARASAIL_FLAG_TRACE
        | PARASAIL_FLAG_BITS_64 | PARASAIL_FLAG_LANES_2;

    /* initialize heap variables */
    s2B= parasail_memalign_int64_t(16, s2Len+PAD2);
    _H_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    _F_pr = parasail_memalign_int64_t(16, s2Len+PAD2);
    s2 = s2B+PAD; /* will allow later for negative indices */
    H_pr = _H_pr+PAD;
    F_pr = _F_pr+PAD;

    /* validate heap variables */
    if (!s2B) return NULL;
    if (!_H_pr) return NULL;
    if (!_F_pr) return NULL;

    /* convert _s1 from char to int in range 0-23 */
    if (matrix->type == PARASAIL_MATRIX_TYPE_SQUARE) {
        s1 = parasail_memalign_int64_t(16, s1Len+PAD);
        if (!s1) return NULL;
        for (i=0; i<s1Len; ++i) {
            s1[i] = matrix->mapper[(unsigned char)_s1[i]];
        }
        /* pad back of s1 with dummy values */
        for (i=s1Len; i<s1Len_PAD; ++i) {
            s1[i] = 0; /* point to first matrix row because we don't care */
        }
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
        simde__m128i vNH = vNegInf;
        simde__m128i vWH = vNegInf;
        simde__m128i vE = vNegInf;
        simde__m128i vE_opn = vNegInf;
        simde__m128i vE_ext = vNegInf;
        simde__m128i vF = vNegInf;
        simde__m128i vF_opn = vNegInf;
        simde__m128i vF_ext = vNegInf;
        simde__m128i vJ = vJreset;
        const int * const restrict matrow0 = &matrix->matrix[matrix->size * ((matrix->type == PARASAIL_MATRIX_TYPE_SQUARE) ? s1[i+0] : ((i+0 >= s1Len) ? s1Len-1 : i+0))];
        const int * const restrict matrow1 = &matrix->matrix[matrix->size * ((matrix->type == PARASAIL_MATRIX_TYPE_SQUARE) ? s1[i+1] : ((i+1 >= s1Len) ? s1Len-1 : i+1))];
        vNH = simde_mm_srli_si128(vNH, 8);
        vNH = simde_mm_insert_epi64(vNH, H_pr[-1], 1);
        vWH = simde_mm_srli_si128(vWH, 8);
        vWH = simde_mm_insert_epi64(vWH, -open - i*gap, 1);
        H_pr[-1] = -open - (i+N)*gap;
        /* iterate over database sequence */
        for (j=0; j<s2Len+PAD; ++j) {
            simde__m128i vMat;
            simde__m128i vNWH = vNH;
            vNH = simde_mm_srli_si128(vWH, 8);
            vNH = simde_mm_insert_epi64(vNH, H_pr[j], 1);
            vF = simde_mm_srli_si128(vF, 8);
            vF = simde_mm_insert_epi64(vF, F_pr[j], 1);
            vF_opn = simde_mm_sub_epi64(vNH, vOpen);
            vF_ext = simde_mm_sub_epi64(vF, vGap);
            vF = simde_mm_max_epi64(vF_opn, vF_ext);
            vE_opn = simde_mm_sub_epi64(vWH, vOpen);
            vE_ext = simde_mm_sub_epi64(vE, vGap);
            vE = simde_mm_max_epi64(vE_opn, vE_ext);
            vMat = simde_mm_set_epi64x(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]]
                    );
            vNWH = simde_mm_add_epi64(vNWH, vMat);
            vWH = simde_mm_max_epi64(vNWH, vE);
            vWH = simde_mm_max_epi64(vWH, vF);
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                simde__m128i cond = simde_mm_cmpeq_epi64(vJ,vNegOne);
                vWH = simde_mm_blendv_epi8(vWH, vIBoundary, cond);
                vF = simde_mm_blendv_epi8(vF, vNegInf, cond);
                vE = simde_mm_blendv_epi8(vE, vNegInf, cond);
            }
            
            /* trace table */
            {
                simde__m128i case1 = simde_mm_cmpeq_epi64(vWH, vNWH);
                simde__m128i case2 = simde_mm_cmpeq_epi64(vWH, vF);
                simde__m128i vT = simde_mm_blendv_epi8(
                        simde_mm_blendv_epi8(vTIns, vTDel, case2),
                        vTDiag,
                        case1);
                simde__m128i condE = simde_mm_cmpgt_epi64(vE_opn, vE_ext);
                simde__m128i condF = simde_mm_cmpgt_epi64(vF_opn, vF_ext);
                simde__m128i vET = simde_mm_blendv_epi8(vTInsE, vTDiagE, condE);
                simde__m128i vFT = simde_mm_blendv_epi8(vTDelF, vTDiagF, condF);
                vT = simde_mm_or_si128(vT, vET);
                vT = simde_mm_or_si128(vT, vFT);
                arr_store_si128(result->trace->trace_table, vT, i, s1Len, j, s2Len);
            }
            H_pr[j-1] = (int64_t)simde_mm_extract_epi64(vWH,0);
            F_pr[j-1] = (int64_t)simde_mm_extract_epi64(vF,0);
            /* as minor diagonal vector passes across table, extract
               last table value at the i,j bound */
            {
                simde__m128i cond_valid_I = simde_mm_cmpeq_epi64(vI, vILimit1);
                simde__m128i cond_valid_J = simde_mm_cmpeq_epi64(vJ, vJLimit1);
                simde__m128i cond_all = simde_mm_and_si128(cond_valid_I, cond_valid_J);
                vMax = simde_mm_blendv_epi8(vMax, vWH, cond_all);
            }
            vJ = simde_mm_add_epi64(vJ, vOne);
        }
        vI = simde_mm_add_epi64(vI, vN);
        vIBoundary = simde_mm_sub_epi64(vIBoundary, vGapN);
    }

    /* max in vMax */
    for (i=0; i<N; ++i) {
        int64_t value;
        value = (int64_t) simde_mm_extract_epi64(vMax, 1);
        if (value > score) {
            score = value;
        }
        vMax = simde_mm_slli_si128(vMax, 8);
    }

    

    result->score = score;
    result->end_query = end_query;
    result->end_ref = end_ref;

    parasail_free(_F_pr);
    parasail_free(_H_pr);
    parasail_free(s2B);
    if (matrix->type == PARASAIL_MATRIX_TYPE_SQUARE) {
        parasail_free(s1);
    }

    return result;
}


