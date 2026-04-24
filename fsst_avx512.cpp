// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
//
// Copyright 2018-2020, CWI, TU Munich, FSU Jena
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
// (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// - The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// You can contact the authors via the FSST source repository : https://github.com/cwida/fsst
#include "libfsst.hpp"

#ifdef FSST_DISABLE_AVX512
namespace libfsst {
bool fsst_hasAVX512() { return false; }
}  // namespace libfsst
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

#ifdef _WIN32
namespace libfsst {
bool fsst_hasAVX512() {
   int info[4];
   __cpuidex(info, 0x00000007, 0);
   return (info[1]>>16)&1;
}
}  // namespace libfsst
#else
#include <cpuid.h>
namespace libfsst {
bool fsst_hasAVX512() {
   int info[4];
   __cpuid_count(0x00000007, 0, info[0], info[1], info[2], info[3]);
   return (info[1]>>16)&1;
}
}  // namespace libfsst
#endif
#else
namespace libfsst {
bool fsst_hasAVX512() { return false; }
}  // namespace libfsst
#endif

namespace libfsst {

// BULK COMPRESSION OF STRINGS
//
// In one call of this function, we can compress 512 strings, each of maximum length 511 bytes.
// strings can be shorter than 511 bytes, no problem, but if they are longer we need to cut them up.
//
// In each iteration of the while loop, we find one code in each of the unroll*8 strings, i.e. (8,16,24 or 32) for resp. unroll=1,2,3,4 
// unroll3 performs best on my hardware
//
// In the worst case, each final encoded string occupies 512KB bytes (512*1024; with 1024=512xexception, exception = 2 bytes).
// - hence codeBase is a buffer of 512KB (needs 19 bits jobs), symbolBase of 256KB (needs 18 bits jobs). 
//
// 'jobX' controls the encoding of each string and is therefore a u64 with format [out:19][pos:9][end:18][cur:18] (low-to-high bits)
// The field 'pos' tells which string we are processing (0..511). We need this info as strings will complete compressing out-of-order.
//
// Strings will have different lengths, and when a string is finished, we reload from the buffer of 512 input strings.
// This continues until we have less than (8,16,24 or 32; depending on unroll) strings left to process.
// - so 'processed' is the amount of strings we started processing and it is between [480,512].
// Note that when we quit, there will still be some (<32) strings that we started to process but which are unfinished.
// - so 'unfinished' is that amount. These unfinished strings will be encoded further using the scalar method.
//
// Apart from the coded strings, we return in a output[] array of size 'processed' the job values of the 'finished' strings.
// In the following 'unfinished' slots (processed=finished+unfinished) we output the 'job' values of the unfinished strings.
// 
// For the finished strings, we need [out:19] to see the compressed size and [pos:9] to see which string we refer to.
// For the unfinished strings, we need all fields of 'job' to continue the compression with scalar code (see SIMD code in compressBatch).
//
// THIS IS A SEPARATE CODE FILE NOT BECAUSE OF MY LOVE FOR MODULARIZED CODE BUT BECAUSE IT ALLOWS TO COMPILE IT WITH DIFFERENT FLAGS
// in particular, unrolling is crucial for gather/scatter performance, but requires registers. the #define all_* expressions however, 
// will be detected to be constants by g++ -O2 and will be precomputed and placed into AVX512 registers - spoiling 9 of them. 
// This reduces the effectiveness of unrolling, hence -O2 makes the loop perform worse than -O1 which skips this optimization. 
// Assembly inspection confirmed that 3-way unroll with -O1 avoids needless load/stores.

size_t fsst_compressAVX512(SymbolTable &symbolTable, u8* codeBase, u8* symbolBase, SIMDjob *input, SIMDjob *output, size_t n, size_t unroll) {
   size_t processed = 0;
   // define some constants (all_x means that all 8 lanes contain 64-bits value X)
#ifdef __AVX512F__
 //__m512i all_suffixLim= _mm512_broadcastq_epi64(_mm_set1_epi64((__m64) (u64) symbolTable->suffixLim)); -- for variants b,c
   __m512i all_MASK     = _mm512_broadcastq_epi64(_mm_set1_epi64((__m64) (u64) -1));
   __m512i all_PRIME    = _mm512_broadcastq_epi64(_mm_set1_epi64((__m64) (u64) FSST_HASH_PRIME));
   __m512i all_ICL_FREE = _mm512_broadcastq_epi64(_mm_set1_epi64((__m64) (u64) FSST_ICL_FREE));
#define    all_HASH       _mm512_srli_epi64(all_MASK, 64-FSST_HASH_LOG2SIZE)
#define    all_ONE        _mm512_srli_epi64(all_MASK, 63)
#define    all_M19        _mm512_srli_epi64(all_MASK, 45)
#define    all_M18        _mm512_srli_epi64(all_MASK, 46)
#define    all_M28        _mm512_srli_epi64(all_MASK, 36)
#define    all_FFFFFF     _mm512_srli_epi64(all_MASK, 40)
#define    all_FFFF       _mm512_srli_epi64(all_MASK, 48)
#define    all_FF         _mm512_srli_epi64(all_MASK, 56)

   SIMDjob *inputEnd = input+n;
   assert(n >= unroll*8 && n <= 512); // should be close to 512 
   __m512i job1, job2, job3, job4; // will contain current jobs, for each unroll 1,2,3,4
   __mmask8 loadmask1 = 255, loadmask2 = 255*(unroll>1), loadmask3 = 255*(unroll>2), loadmask4 = 255*(unroll>3); // 2b loaded new strings bitmask per unroll
   u32 delta1 = 8, delta2 = 8*(unroll>1), delta3 = 8*(unroll>2), delta4 = 8*(unroll>3); // #new loads this SIMD iteration per unroll

   if (unroll >= 4) {
      while (input+delta1+delta2+delta3+delta4 < inputEnd) {
         #include "fsst_avx512_unroll4.inc"
      }
   } else if (unroll == 3) {
      while (input+delta1+delta2+delta3 < inputEnd) {
         #include "fsst_avx512_unroll3.inc"
      }
   } else if (unroll == 2) {
      while (input+delta1+delta2 < inputEnd) {
         #include "fsst_avx512_unroll2.inc"
      }
   } else {
      while (input+delta1 < inputEnd) {
         #include "fsst_avx512_unroll1.inc"
      }
   }

   // flush the job states of the unfinished strings at the end of output[] 
   processed = n - (inputEnd - input);
   u32 unfinished = 0;
   if (unroll > 1) { 
      if (unroll > 2) { 
         if (unroll > 3) { 
            _mm512_mask_compressstoreu_epi64(output+unfinished, loadmask4=~loadmask4, job4); 
            unfinished += _mm_popcnt_u32((int) loadmask4);
         }
         _mm512_mask_compressstoreu_epi64(output+unfinished, loadmask3=~loadmask3, job3); 
         unfinished += _mm_popcnt_u32((int) loadmask3);
      }
      _mm512_mask_compressstoreu_epi64(output+unfinished, loadmask2=~loadmask2, job2); 
      unfinished += _mm_popcnt_u32((int) loadmask2);
   }
   _mm512_mask_compressstoreu_epi64(output+unfinished, loadmask1=~loadmask1, job1); 
#else
   (void) symbolTable;
   (void) codeBase;
   (void) symbolBase;
   (void) input;
   (void) output;
   (void) n;
   (void) unroll;
#endif
   return processed;
}

void SymbolTable::buildDP_avx512( const u8* data, size_t n, bool finalLayout) {
      #ifdef __AVX512F__
      if (finalLayout) {
         if (!bucketReadyFinal) rebuildBuckets(true);
      } else {
         if (!bucketReadyTrain) rebuildBuckets(false);
      }

      dpCost.resize(n + 8);
      for (size_t t = 0; t < 8; ++t) dpCost[n + t] = 0;
      dpChoice.resize(n);

      const __m512i VZERO64 = _mm512_setzero_si512();
      const __m512i VONE32  = _mm512_set1_epi32(1);
      const __m512i VINF32  = _mm512_set1_epi32((int)DP_INF_COST);

      const __m512i REDUCE4 = _mm512_setr_epi64(4,5,6,7,0,1,2,3);
      const __m512i REDUCE2 = _mm512_setr_epi64(2,3,0,1,6,7,4,5);
      const __m512i REDUCE1 = _mm512_setr_epi64(1,0,3,2,5,4,7,6);
      // Rolling DP window update:
      // after putting new dpCost[i] in lane 15,
      // low 8 lanes become [15,0,1,2,3,4,5,6]
      const __m512i SHIFT_DP = _mm512_setr_epi32(
         15, 0, 1, 2, 3, 4, 5, 6,
         7, 8, 9,10,11,12,13,14
      );
      __m512i dpWin = _mm512_setzero_si512();

      u64 w = 0;
      for (int i = (int)n - 1; i >= 0; --i) {
         w = (w << 8) | data[i];
         u8 b = (u8)w;
         u16 key = (u16)w;

         u16 litCode = byteCodes[b] & FSST_CODE_MASK;
         u32 litEmit = finalLayout ? (1u + (litCode == 511)) : (1u + (litCode < FSST_CODE_BASE));
         u32 litCost = litEmit + dpCost[i + 1];
         u64 litKey  = packDPKey(litCost, 1, litCode);

         __m512i bestKeyV = _mm512_set1_epi64((long long)litKey);
         __m512i WV       = _mm512_set1_epi64((long long)w);

         Bucket bk = bucket2[key];

         for (u32 p = bk.first; p < bk.first + bk.count8; p += 8) {
            __m512i bytesV = _mm512_loadu_si512((const void*)&candBytes[p]);
            __m512i masksV = _mm512_loadu_si512((const void*)&candMasks[p]);
            __m512i diffV  = _mm512_and_si512(_mm512_xor_si512(WV, bytesV), masksV);

            __mmask8 matchMask = _mm512_cmpeq_epi64_mask(diffV, VZERO64);

            __m512i validV = _mm512_loadu_si512((const void*)&candValid64[p]);
            __mmask8 validMask = ~_mm512_cmpeq_epi64_mask(validV, VZERO64);

            matchMask &= validMask;

            __m512i idxV = _mm512_maskz_loadu_epi32(0x00FF, &candLenIdx32[p]);
            __m512i nxtV = _mm512_permutexvar_epi32(idxV, dpWin);

            __m512i cost32V = _mm512_add_epi32(nxtV, VONE32);
            cost32V = _mm512_mask_mov_epi32(VINF32, (__mmask16)matchMask, cost32V);

            __m256i cost32Lo = _mm512_castsi512_si256(cost32V);
            __m512i cost64V  = _mm512_cvtepu32_epi64(cost32Lo);

            __m512i tieCodeV = _mm512_loadu_si512((const void*)&candTieCode64[p]);
            __m512i keyV     = _mm512_or_si512(_mm512_slli_epi64(cost64V, 12), tieCodeV);

            bestKeyV = _mm512_min_epi64(bestKeyV, keyV);
         }

         // horizontal reduction over 8 lanes
         __m512i tmp = _mm512_permutexvar_epi64(REDUCE4, bestKeyV);
         bestKeyV = _mm512_min_epi64(bestKeyV, tmp);

         tmp = _mm512_permutexvar_epi64(REDUCE2, bestKeyV);
         bestKeyV = _mm512_min_epi64(bestKeyV, tmp);

         tmp = _mm512_permutexvar_epi64(REDUCE1, bestKeyV);
         bestKeyV = _mm512_min_epi64(bestKeyV, tmp);

         u64 bestKey;
         _mm_storel_epi64((__m128i*)&bestKey, _mm512_castsi512_si128(bestKeyV));

         dpCost[i]   = (u32)(bestKey >> 12);
         dpChoice[i] = (u16)(bestKey & FSST_CODE_MASK);

         // Update rolling DP window for next iteration (i-1):
         // current low lanes are [dp[i+1], ..., dp[i+8]]
         // write dp[i] into lane 15, then permute so next low lanes are
         // [dp[i], dp[i+1], ..., dp[i+7]]
         dpWin = _mm512_mask_set1_epi32(dpWin, 0x8000, (int)dpCost[i]);
         dpWin = _mm512_permutexvar_epi32(SHIFT_DP, dpWin);
      }

      #else
         (void) data;
         (void) n;
         (void) finalLayout;
      #endif
      return;
   }


}  // namespace libfsst
