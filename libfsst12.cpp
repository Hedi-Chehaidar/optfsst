// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
//
// Copyright 2018-2019, CWI, TU Munich
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
#include "libfsst12.hpp"
#include <math.h>
#include <string.h>

namespace libfsst {
Symbol concat(Symbol a, Symbol b) {
   Symbol s;
   u32 length = min(8, a.length()+b.length());
   s.set_code_len(FSST_CODE_MASK, length);
   *(u64*) s.symbol = ((*(u64*) b.symbol) << (8*a.length())) | *(u64*) a.symbol;
   return s;
}
}  // namespace libfsst

namespace std {
template <>
class hash<libfsst::Symbol> {
   public:
   size_t operator()(const libfsst::Symbol& s) const {
      using namespace libfsst;
      uint64_t k = *(u64*) s.symbol;
      const uint64_t m = 0xc6a4a7935bd1e995;
      const int r = 47;
      uint64_t h = 0x8445d61a4e774912 ^ (8*m);
      k *= m;
      k ^= k >> r;
      k *= m;
      h ^= k;
      h *= m;
      h ^= h >> r;
      h *= m;
      h ^= h >> r;
      return h;
   }
};
}

namespace libfsst {
std::ostream& operator<<(std::ostream& out, const Symbol& s) {
   for (u32 i=0; i<s.length(); i++)
      out << s.symbol[i];
   return out;
}

#define FSST_SAMPLETARGET (1<<17) 
#define FSST_SAMPLEMAXSZ ((long) 2*FSST_SAMPLETARGET) 

static inline u32 pack2(u16 a, u16 b) {
   return ((u32)a << 12) | (u32)b;
}

struct Count3 {
   unordered_map<u64, u16> m;

   static inline u64 key(u16 a, u16 b, u16 c) {
      return ((u64)a << 24) | ((u64)b << 12) | (u64)c;
   }

   void clear() { m.clear(); }
   void inc(u16 a, u16 b, u16 c) {
      u64 k = key(a, b, c);
      auto it = m.find(k);
      if (it == m.end()) m.emplace(k, 1);
      else it->second++;
   }
   u16 get(u16 a, u16 b, u16 c) const {
      auto it = m.find(key(a, b, c));
      return it == m.end() ? 0 : it->second;
   }
};

struct Cand {
   u32 gain;
   u16 a, b, c;
   u16 cnt;
   u8 symLen;

   bool operator<(Cand const& other) const { return gain < other.gain; }
};

SymbolMap *buildSymbolMap(Counters& counters, long sampleParam, vector<ulong>& sample, const ulong len[], const u8* line[]) {
   ulong sampleSize = max(sampleParam, FSST_SAMPLEMAXSZ); // if sampleParam is negative, we need to ignore part of the last line
   SymbolMap *st = new SymbolMap(), *bestMap = new SymbolMap();
   long bestGain = -sampleSize; // worst case (everything exception)
   ulong sampleFrac = 128;

   for(ulong i=0; i<sample.size(); i++) {
      const u8* cur = line[sample[i]];
      if (sampleParam < 0 && i+1 == sample.size())
         cur -= sampleSize; // use only last part of last line (which could be too long for an efficient sample)
   }

   // a random number between 0 and 128
   auto rnd128 = [&](ulong i) { return 1 + (FSST_HASH((i+1)*sampleFrac)&127); };

   // compress sample, and compute (pair-)frequencies
   auto compressCount = [&](SymbolMap *st, Counters &counters) { // returns gain
      long gain = 0;

      for(ulong i=0; i<sample.size(); i++) {
         const u8* cur = line[sample[i]];
         const u8* end = cur + len[sample[i]];

         if (sampleParam < 0 && i+1 == sample.size()) { 
            cur -= sampleParam; // use only last part of last line (which could be too long for an efficient sample)
            if ((end-cur) > 500) end = cur + ((end-cur)*sampleFrac)/128; // shorten long lines to the sample fraction
         } else if (sampleFrac < 128) {
            // in earlier rounds (sampleFrac < 128) we skip data in the sample (reduces overall work ~2x)
            if (rnd128(i) > sampleFrac) continue;
         }

         if (cur < end) {
            u16 pos2 = 0, pos1 = st->findExpansion(Symbol(cur, end));
            cur += pos1 >> 12;
            pos1 &= FSST_CODE_MASK;
            while (true) {
	       const u8 *old = cur;
               counters.count1Inc(pos1);
               if (cur<end-7) {
                  ulong word = fsst_unaligned_load(cur);
                  ulong pos = (u32) word; // key is first 4 bytes!!
                  ulong idx = FSST_HASH(pos)&(st->hashTabSize-1);
                  Symbol s = st->hashTab[idx];
                  pos2 = st->shortCodes[word & 0xFFFF];
                  word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.gcl);
                  if ((s.gcl < FSST_GCL_FREE) && (*(u64*) s.symbol == word)) {
                     pos2 = s.code(); cur += s.length();
                  } else {
                     cur += (pos2 >> 12);
                     pos2 &= FSST_CODE_MASK;
                  }
               } else if (cur==end) {
                  break;
               } else {
                  assert(cur<end);
                  pos2 = st->findExpansion(Symbol(cur, end));
                  cur += pos2 >> 12;
                  pos2 &= FSST_CODE_MASK;
               }

               // compute compressed output size (later divide by 2)
               gain += 2*(cur-old)-3;

               // now count the subsequent two symbols we encode as an extension possibility
               if (sampleFrac < 128) { // no need to count pairs in final round
                  counters.count2Inc(pos1, pos2);
               }
               pos1 = pos2;
            }
         }
      }
      return gain; 
   };

   auto makeMap = [&](SymbolMap *st, Counters &counters) {
      // hashmap of c (needed because we can generate duplicate candidates)
      unordered_set<Symbol> cands;

      auto addOrInc = [&](unordered_set<Symbol> &cands, Symbol s, u32 count) {
         auto it = cands.find(s);
         s.gain = s.length()*count;
         if (it != cands.end()) {
            s.gain += (*it).gain;
            cands.erase(*it);
         }
         cands.insert(s);
      };

      // add candidate symbols based on counted frequency
      for (u32 pos1=0; pos1<st->symbolCount; pos1++) { 
         u32 cnt1 = counters.count1GetNext(pos1); // may advance pos1!!
         if (!cnt1) continue;

         Symbol s1 = st->symbols[pos1];
         if (s1.length() > 1) { // 1-byte symbols are always in the map
            addOrInc(cands, s1, cnt1);
         }

         if (sampleFrac >= 128 || // last round we do not create new (combined) symbols
             s1.length() == Symbol::maxLength) { // symbol cannot be extended
            continue;
         }
         for (u32 pos2=0; pos2<st->symbolCount; pos2++) { 
            u32 cnt2 = counters.count2GetNext(pos1, pos2); // may advance pos2!!
            if (!cnt2) continue;

            // create a new symbol
            Symbol s2 = st->symbols[pos2];
            Symbol s3 = concat(s1, s2);
            addOrInc(cands, s3, cnt2);
         }
      }

      // insert candidates into priority queue (by gain)
      auto cmpGn = [](const Symbol& q1, const Symbol& q2) { return q1.gain < q2.gain; };
      priority_queue<Symbol,vector<Symbol>,decltype(cmpGn)> pq(cmpGn);
      for (auto& q : cands)
         pq.push(q);

      // Create new symbol map using best candidates
      st->clear();
      while (st->symbolCount < 4096 && !pq.empty()) {
         Symbol s = pq.top();
         pq.pop();
         st->add(s);
      }
   };

#ifdef NONOPT_FSST
   for(ulong frac : {127, 127, 127, 127, 127, 127, 127, 127, 127, 128}) {
      sampleFrac = frac;
#else
   for(sampleFrac=14; true; sampleFrac = sampleFrac + 38) {
#endif
      memset(&counters, 0, sizeof(Counters));
      long gain = compressCount(st, counters);
      if (gain >= bestGain) { // a new best solution!
         *bestMap = *st; bestGain = gain;
      } 
      if (sampleFrac >= 128) break; // we do 4 rounds (sampleFrac=14,52,90,128)
      makeMap(st, counters);
   }
   delete st;
   return bestMap;
}

SymbolMap *Btrfsst_buildSymbolMap(Counters& counters,
                                    long sampleParam,
                                    vector<ulong>& sample,
                                    const ulong len[],
                                    const u8* line[],
                                    const fsst_options_t& opt) {
   ulong sampleSize = max(sampleParam, FSST_SAMPLEMAXSZ);
   SymbolMap *st = new SymbolMap(), *bestMap = new SymbolMap();
   long bestGain = -sampleSize;
   ulong sampleFrac = 128;

   for(ulong i=0; i<sample.size(); i++) {
      const u8* cur = line[sample[i]];
      if (sampleParam < 0 && i+1 == sample.size())
         cur -= sampleSize;
   }

   auto rnd128 = [&](ulong i) { return 1 + (FSST_HASH((i+1)*sampleFrac)&127); };
   Count3 count3;

   auto compressCount = [&](SymbolMap *st, Counters &counters) {
      long gain = 0;

      for(ulong i=0; i<sample.size(); i++) {
         const u8* cur = line[sample[i]];
         const u8* end = cur + len[sample[i]];

         if (sampleParam < 0 && i+1 == sample.size()) {
            cur -= sampleParam;
            if ((end-cur) > 500) end = cur + ((end-cur)*sampleFrac)/128;
         } else if (sampleFrac < 128) {
            if (rnd128(i) > sampleFrac) continue;
         }

         if (cur < end) {
            u16 pos2 = 0, pos1 = st->findExpansion(Symbol(cur, end));
            cur += pos1 >> 12;
            pos1 &= FSST_CODE_MASK;
            while (true) {
               const u8 *old = cur;
               counters.count1Inc(pos1);
               if (cur<end-7) {
                  ulong word = fsst_unaligned_load(cur);
                  ulong pos = (u32) word;
                  ulong idx = FSST_HASH(pos)&(st->hashTabSize-1);
                  Symbol s = st->hashTab[idx];
                  pos2 = st->shortCodes[word & 0xFFFF];
                  word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.gcl);
                  if ((s.gcl < FSST_GCL_FREE) && (*(u64*) s.symbol == word)) {
                     pos2 = s.code(); cur += s.length();
                  } else {
                     cur += (pos2 >> 12);
                     pos2 &= FSST_CODE_MASK;
                  }
               } else if (cur==end) {
                  break;
               } else {
                  pos2 = st->findExpansion(Symbol(cur, end));
                  cur += pos2 >> 12;
                  pos2 &= FSST_CODE_MASK;
               }

               gain += 2*(cur-old)-3;

               if (sampleFrac < 128) {
                  counters.count2Inc(pos1, pos2);
               }
               pos1 = pos2;
            }
         }
      }
      return gain;
   };

   auto compressCountDP = [&](SymbolMap *st, Counters &counters) {
      long gain = 0;
      count3.clear();

      for (ulong i = 0; i < sample.size(); i++) {
         const u8* cur = line[sample[i]];
         const u8* end = cur + len[sample[i]];

         if (sampleParam < 0 && i+1 == sample.size()) {
            cur -= sampleParam;
            if ((end-cur) > 500) end = cur + ((end-cur)*sampleFrac)/128;
         } else if (sampleFrac < 128) {
            if (rnd128(i) > sampleFrac) continue;
         }

         size_t n = (size_t)(end - cur);
         if (n == 0) continue;

         st->buildDP_scalar(cur, n);

         u16 prev1 = 0xFFFF;
         u16 prev2 = 0xFFFF;
         size_t pos = 0;
         while (pos < n) {
            u16 code = st->dpChoice[pos];
            long L = st->symbols[code].length();
            gain += 2 * L - 3;
            counters.count1Inc(code);

            size_t nextPos = pos + L;
            if (nextPos < n) {
               u16 next = st->dpChoice[nextPos];
               if (sampleFrac < 128) counters.count2Inc(code, next);
               prev2 = prev1;
               prev1 = code;
               if (sampleFrac < 128 && (opt.flags & FSST_OPT_TRIPLES) && prev2 != 0xFFFF) {
                  u32 Lprevs = st->symbols[prev2].length() + st->symbols[prev1].length();
                  if (Lprevs < Symbol::maxLength) count3.inc(prev2, prev1, next);
               }
            }
            pos = nextPos;
         }
      }
      return gain;
   };

   auto makeMapEx = [&](SymbolMap *st, Counters &counters) {
      const u32 C = st->symbolCount;
      vector<Symbol> prevSym(C);
      for (u32 i = 0; i < C; ++i) prevSym[i] = st->symbols[i];

      vector<int> c1(C, 0);
      unordered_map<u32, int> c2;

      for (u32 pos1 = 0; pos1 < C; pos1++) {
         u32 cnt1 = counters.count1GetNext(pos1);
         if (!cnt1) continue;
         c1[pos1] = (int)cnt1;

         if (sampleFrac >= 128) continue;
         for (u32 pos2 = 0; pos2 < C; pos2++) {
            u32 cnt2 = counters.count2GetNext(pos1, pos2);
            if (!cnt2) continue;
            c2.emplace(pack2((u16)pos1, (u16)pos2), (int)cnt2);
         }
      }

      priority_queue<Cand> heap;

      auto pairCount = [&](u16 a, u16 b) -> int {
         auto it = c2.find(pack2(a, b));
         return it == c2.end() ? 0 : it->second;
      };

      auto pushSingle = [&](u16 a) {
         if (a >= C) return;
         int cnt = c1[a];
         if (cnt <= 0) return;
         u8 L = (u8)prevSym[a].length();
         if (L <= 1) return;
         heap.push(Cand{(u32)(L * cnt), a, 0xFFFF, 0xFFFF, (u16)cnt, L});
      };

      auto pushPair = [&](u16 a, u16 b) {
         int cnt = pairCount(a, b);
         if (cnt <= 0) return;
         u32 Lsum = prevSym[a].length() + prevSym[b].length();
         if (Lsum > Symbol::maxLength) Lsum = Symbol::maxLength;
         heap.push(Cand{(u32)(Lsum * cnt), a, b, 0xFFFF, (u16)cnt, (u8)Lsum});
      };

      auto pushTriple = [&](u16 a, u16 b, u16 c) {
         u16 cnt = count3.get(a, b, c);
         if (!cnt) return;
         u32 Lab = prevSym[a].length() + prevSym[b].length();
         if (Lab >= Symbol::maxLength) return;
         u32 Lsum = Lab + prevSym[c].length();
         if (Lsum > Symbol::maxLength) Lsum = Symbol::maxLength;
         heap.push(Cand{(u32)(Lsum * cnt), a, b, c, cnt, (u8)Lsum});
      };

      for (u16 a = 0; a < C; ++a) if (c1[a]) pushSingle(a);
      if (sampleFrac < 128) {
         for (auto const& kv : c2) {
            u16 a = (u16)(kv.first >> 12);
            u16 b = (u16)(kv.first & FSST_CODE_MASK);
            if (prevSym[a].length() == Symbol::maxLength) continue;
            pushPair(a, b);
         }
      }
      if ((opt.flags & FSST_OPT_TRIPLES) && sampleFrac < 128) {
         for (auto const& kv : count3.m) {
            u64 k = kv.first;
            u16 a = (u16)(k >> 24);
            u16 b = (u16)((k >> 12) & FSST_CODE_MASK);
            u16 c = (u16)(k & FSST_CODE_MASK);
            pushTriple(a, b, c);
         }
      }

      st->clear();
      unordered_set<u64> seen;
      while (st->symbolCount < 4096 && !heap.empty()) {
         Cand cd = heap.top();
         heap.pop();

         int curCnt = 0;
         if (cd.b == 0xFFFF) curCnt = c1[cd.a];
         else if (cd.c == 0xFFFF) curCnt = pairCount(cd.a, cd.b);
         else curCnt = (int)count3.get(cd.a, cd.b, cd.c);

         if (curCnt != cd.cnt || curCnt <= 0) continue;

         Symbol s;
         if (cd.b == 0xFFFF) {
            s = prevSym[cd.a];
         } else if (cd.c == 0xFFFF) {
            s = concat(prevSym[cd.a], prevSym[cd.b]);
         } else {
            Symbol ab = concat(prevSym[cd.a], prevSym[cd.b]);
            s = concat(ab, prevSym[cd.c]);
         }

         u32 len = s.length();
         if (len == 1) continue;
         if(len > 2 && (opt.flags & FSST_OPT_DP_TRAIN) && (opt.flags & FSST_OPT_DP_ENCODE) ) { // don't use fsst hashing
            u64 num = *(u64*)s.symbol;
            if(seen.find(num) != seen.end()) continue;
            seen.insert(num);
            u32 len = s.length();
            // add symbol to st
            s.set_code_len(st->symbolCount, len);
            st->symbols[st->symbolCount++] = s;
            st->lenHisto[len-1]++;
         }
         else if (!st->add(s)) continue;

         if ((opt.flags & FSST_OPT_PRUNE) && cd.b != 0xFFFF) {
            int used = curCnt / 2;
            c1[cd.a] -= used;
            if (c1[cd.a] > 0) pushSingle(cd.a);
            c1[cd.b] -= used;
            if (c1[cd.b] > 0) pushSingle(cd.b);
            if (cd.c != 0xFFFF) {
               c1[cd.c] -= used;
               if (c1[cd.c] > 0) pushSingle(cd.c);
               auto itAB = c2.find(pack2(cd.a, cd.b));
               if (itAB != c2.end()) {
                  itAB->second -= used;
                  if (itAB->second > 0) pushPair(cd.a, cd.b);
               }
               auto itBC = c2.find(pack2(cd.b, cd.c));
               if (itBC != c2.end()) {
                  itBC->second -= used;
                  if (itBC->second > 0) pushPair(cd.b, cd.c);
               }
            }
         }
      }
   };

   for(ulong frac : {127, 127, 127, 128}) {
      sampleFrac = frac;
   //for(sampleFrac=14; true; sampleFrac = sampleFrac + 38) {
      memset(&counters, 0, sizeof(Counters));
      long gain = (opt.flags & FSST_OPT_DP_TRAIN) ? compressCountDP(st, counters)
                                                  : compressCount(st, counters);
      if (gain >= bestGain) {
         *bestMap = *st;
         bestGain = gain;
      }
      if (sampleFrac >= 128) break;
      makeMapEx(st, counters);
   }

   delete st;
   return bestMap;
}

// optimized adaptive *scalar* compression method
static inline ulong compressBulk(SymbolMap &symbolMap, ulong nlines, const ulong lenIn[], const u8* strIn[], ulong size, u8* out, ulong lenOut[], u8* strOut[]) {
   u8 *lim = out + size;
   ulong curLine;
   for(curLine=0; curLine<nlines; curLine++) {
      const u8 *cur = strIn[curLine];
      const u8 *end = cur + lenIn[curLine];
      strOut[curLine] = out;
      while (cur+16 <= end && (lim-out) >= 8) {
         u64 word = fsst_unaligned_load(cur);
         ulong code = symbolMap.shortCodes[word & 0xFFFF];
         ulong pos = (u32) word; // key is first 4 bytes
         ulong idx = FSST_HASH(pos)&(symbolMap.hashTabSize-1);
         Symbol s = symbolMap.hashTab[idx];
         word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.gcl);
         if ((s.gcl < FSST_GCL_FREE) && *(ulong*) s.symbol == word) {
            code = s.gcl >> 16;
         }
         cur += (code >> 12);
         u32 res = code & FSST_CODE_MASK;
         word = fsst_unaligned_load(cur);
         code = symbolMap.shortCodes[word & 0xFFFF];
         pos = (u32) word; // key is first 4 bytes
         idx = FSST_HASH(pos)&(symbolMap.hashTabSize-1);
         s = symbolMap.hashTab[idx];
         word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.gcl);
         if ((s.gcl < FSST_GCL_FREE) && *(ulong*) s.symbol == word) {
           code = s.gcl >> 16;
         }
         cur += (code >> 12);
         res |= (code&FSST_CODE_MASK) << 12;
         memcpy(out, &res, sizeof(u32));
         out += 3; 
      }
      while (cur < end) {
         ulong code = symbolMap.findExpansion(Symbol(cur, end));
         u32 res = (code&FSST_CODE_MASK);
         if (out+8 > lim) {
             return curLine; // u32 write would be out of bounds (out of output memory) 
         }
         cur += code >> 12;
         if (cur >= end) {
            memcpy(out, &res, sizeof(u32));
	    out += 2;
            break;
         }
         code = symbolMap.findExpansion(Symbol(cur, end));
         res |= (code&FSST_CODE_MASK) << 12;
         cur += code >> 12;
         memcpy(out, &res, sizeof(u32));
	 out += 3;
      } 
      lenOut[curLine] = out - strOut[curLine];
   } 
   return curLine;
}

static inline ulong compressBulkDP(SymbolMap &symbolMap,
                                   ulong nlines,
                                   const ulong lenIn[],
                                   const u8* strIn[],
                                   ulong size,
                                   u8* out,
                                   ulong lenOut[],
                                   u8* strOut[]) {
   u8 *lim = out + size;

   for (ulong curLine = 0; curLine < nlines; curLine++) {
      const u8 *cur = strIn[curLine];
      const u8 *end = cur + lenIn[curLine];
      strOut[curLine] = out;

      // Unlike the 8-bit path, fsst12 stores two 12-bit codes across 3 bytes.
      // That means we cannot independently encode fixed-size chunks and simply
      // concatenate them: a chunk may end with a 2-byte single-code tail, and
      // such tails are only valid at the very end of the whole string.
      //
      // So the DP encoder must process the entire string as one code stream.
      if ((2 * lenIn[curLine] + 3) > (ulong)(lim - out)) {
         return curLine;
      }

      symbolMap.buildDP_scalar(cur, lenIn[curLine]);

      size_t pos = 0;
      while (pos < lenIn[curLine]) {
         u16 code0 = symbolMap.dpChoice[pos];
         pos += symbolMap.symbols[code0].length();

         if (pos < lenIn[curLine]) {
            u16 code1 = symbolMap.dpChoice[pos];
            u32 packed = (u32)code0 | ((u32)code1 << 12);
            memcpy(out, &packed, 3);
            out += 3;
            pos += symbolMap.symbols[code1].length();
         } else {
            memcpy(out, &code0, 2);
            out += 2;
         }
      }

      lenOut[curLine] = out - strOut[curLine];
   }

   return nlines;
}

long makeSample(vector<ulong> &sample, ulong nlines, const ulong len[]) {
   ulong i, sampleRnd = 1, sampleProb = 256, sampleSize = 0, totSize = 0;
   ulong sampleTarget = FSST_SAMPLETARGET;

   for(i=0; i<nlines; i++) 
      totSize += len[i];

   if (totSize > FSST_SAMPLETARGET) {
      // if the batch is larger than the sampletarget, sample this fraction  
      sampleProb = max(((ulong) 4),(256*sampleTarget) / totSize);
   } else {
      // too little data. But ok, do not include lines multiple times, just use everything once
      sampleTarget = totSize; // sampleProb will be 256/256 (aka 100%) 
   } 
   do {
      // if nlines is very large and strings are small (8, so we need 4K lines), we still expect 4K*256/4 iterations total worst case
      for(i=0; i<nlines; i++) { 
         // cheaply draw a random number to select (or not) each line
         sampleRnd = FSST_HASH(sampleRnd);
         if ((sampleRnd&255) < sampleProb) {
            sample.push_back(i);
            sampleSize += len[i];
            if (sampleSize >= sampleTarget) // enough? 
               i = nlines; // break out of both loops; 
         }
      }
      sampleProb *= 4; //accelerate the selection process at expense of front-bias (4,16,64,256: 4 passes max)
   } while(i <= nlines); // basically continue until we have enough

   // if the last line (only line?) is excessively long, return a negative samplesize (the amount of front bytes to skip)
   long sampleLong = (long) sampleSize;
   assert(sampleLong > 0);
   return (sampleLong < FSST_SAMPLEMAXSZ)?sampleLong:FSST_SAMPLEMAXSZ-sampleLong; 
}
}  // namespace libfsst

using namespace libfsst; 
extern "C" fsst_encoder_t* fsst_create(ulong n, const ulong lenIn[], const u8 *strIn[], int dummy) {
   vector<ulong> sample;
   (void) dummy;
   long sampleSize = makeSample(sample, n?n:1, lenIn); // careful handling of input to get a right-size and representative sample
   Encoder *encoder = new Encoder();
   encoder->symbolMap = shared_ptr<SymbolMap>(buildSymbolMap(encoder->counters, sampleSize, sample, lenIn, strIn));
   return (fsst_encoder_t*) encoder;
}

extern "C" fsst_encoder_t* Btrfsst_create(ulong n,
                                            const ulong lenIn[],
                                            const u8 *strIn[],
                                            int dummy,
                                            const fsst_options_t* optp) {
   vector<ulong> sample;
   (void) dummy;
   long sampleSize = makeSample(sample, n?n:1, lenIn);
   Encoder *encoder = new Encoder();
   fsst_options_t opt = optp ? *optp : fsst_options_t{0};
   unsigned train_flags = opt.flags & (FSST_OPT_DP_TRAIN | FSST_OPT_TRIPLES | FSST_OPT_PRUNE);

   if (train_flags == 0) {
      encoder->symbolMap = shared_ptr<SymbolMap>(buildSymbolMap(encoder->counters, sampleSize, sample, lenIn, strIn));
   } else {
      encoder->symbolMap = shared_ptr<SymbolMap>(Btrfsst_buildSymbolMap(encoder->counters, sampleSize, sample, lenIn, strIn, opt));
   }

   return (fsst_encoder_t*) encoder;
}

/* create another encoder instance, necessary to do multi-threaded encoding using the same dictionary */
extern "C" fsst_encoder_t* fsst_duplicate(fsst_encoder_t *encoder) {
  Encoder *e = new Encoder();
   e->symbolMap = ((Encoder*)encoder)->symbolMap; // it is a shared_ptr
   return (fsst_encoder_t*) e;
}

// export a dictionary in compact format. 
extern "C" u32 fsst_export(fsst_encoder_t *encoder, u8 *buf) {
  Encoder *e = (Encoder*) encoder;
   // In ->version there is a versionnr, but we hide also suffixLim/terminator/symbolCount there.
   // This is sufficient in principle to *reconstruct* a fsst_encoder_t from a fsst_decoder_t
   // (such functionality could be useful to append compressed data to an existing block).
   //
   // However, the hash function in the encoder hash table is endian-sensitive, and given its
   // 'lossy perfect' hashing scheme is *unable* to contain other-endian-produced symbol tables.
   // Doing a endian-conversion during hashing will be slow and self-defeating.
   //
   // Overall, we could support reconstructing an encoder for incremental compression, but 
   // should enforce equal-endianness. Bit of a bummer. Not going there now.
   // 
   // The version field is now there just for future-proofness, but not used yet
   
   // version allows keeping track of fsst versions, track endianness, and encoder reconstruction
   u64 version = (FSST_VERSION << 32) | FSST_ENDIAN_MARKER; // least significant byte is nonzero

   /* do not assume unaligned reads here */
   memcpy(buf, &version, 8);
   memcpy(buf+8, e->symbolMap->lenHisto, 16); // serialize the lenHisto
   u32 pos = 24;

   // emit only the used bytes of the symbols 
   for(u32 i = 0; i < e->symbolMap->symbolCount; i++) {
      buf[pos++] = e->symbolMap->symbols[i].length();
      for(u32 j = 0; j < e->symbolMap->symbols[i].length(); j++) {
         buf[pos++] = ((u8*) &e->symbolMap->symbols[i].symbol)[j]; // serialize used symbol bytes
      }
   }
   return pos; // length of what was serialized
}

#define FSST_CORRUPT 32774747032022883 /* 7-byte number in little endian containing "corrupt" */

extern "C" u32 fsst_import(fsst_decoder_t *decoder, u8 *buf) {
   u64 version = 0, symbolCount = 0;
   u32 pos = 24;
   u16 lenHisto[8];

   // version field (first 8 bytes) is now there just for future-proofness, unused still (skipped)
   memcpy(&version, buf, 8);
   if ((version>>32) != FSST_VERSION) return 0;
   memcpy(lenHisto, buf+8, 16);

   for(u32 i=0; i<8; i++) 
     symbolCount += lenHisto[i]; 

   for(u32 i = 0; i < symbolCount; i++) {
      u32 len = decoder->len[i] = buf[pos++];
      for(u32 j = 0; j < len; j++) {
        ((u8*) &decoder->symbol[i])[j] = buf[pos++];
      }
   }
   // fill unused symbols with text "corrupt". Gives a chance to detect corrupted code sequences (if there are unused symbols).
   while(symbolCount<4096) {
       decoder->symbol[symbolCount] = FSST_CORRUPT;    
       decoder->len[symbolCount++] = 8;
   }
   return pos;
}

namespace libfsst {
// runtime check for simd
inline ulong _compressImpl(Encoder *e, ulong nlines, const ulong lenIn[], const u8 *strIn[], ulong size, u8 *output, ulong *lenOut, u8 *strOut[], bool noSuffixOpt, bool avoidBranch, int simd) {
   (void) noSuffixOpt;
   (void) avoidBranch;
   (void) simd;
   return compressBulk(*e->symbolMap, nlines, lenIn, strIn, size, output, lenOut, strOut);
}
ulong compressImpl(Encoder *e, ulong nlines, const ulong lenIn[], const u8 *strIn[], ulong size, u8 *output, ulong *lenOut, u8 *strOut[], bool noSuffixOpt, bool avoidBranch, int simd) {
   return _compressImpl(e, nlines, lenIn, strIn, size, output, lenOut, strOut, noSuffixOpt, avoidBranch, simd);
}

// adaptive choosing of scalar compression method based on symbol length histogram 
inline ulong _compressAuto(Encoder *e, ulong nlines, const ulong lenIn[], const u8 *strIn[], ulong size, u8 *output, ulong *lenOut, u8 *strOut[], int simd) {
   (void) simd;
   return _compressImpl(e, nlines, lenIn, strIn, size, output, lenOut, strOut, false, false, false);
}
ulong compressAuto(Encoder *e, ulong nlines, const ulong lenIn[], const u8 *strIn[], ulong size, u8 *output, ulong *lenOut, u8 *strOut[], int simd) {
   return _compressAuto(e, nlines, lenIn, strIn, size, output, lenOut, strOut, simd);
}
}  // namespace libfsst

using namespace libfsst;
// the main compression function (everything automatic)
extern "C" ulong fsst_compress(fsst_encoder_t *encoder, ulong nlines, const ulong lenIn[], const u8 *strIn[], ulong size, u8 *output, ulong *lenOut, u8 *strOut[]) {
   // to be faster than scalar, simd needs 64 lines or more of length >=12; or fewer lines, but big ones (totLen > 32KB)
   ulong totLen = accumulate(lenIn, lenIn+nlines, 0);
   int simd = totLen > nlines*12 && (nlines > 64 || totLen > (ulong) 1<<15); 
   return _compressAuto((Encoder*) encoder, nlines, lenIn, strIn, size, output, lenOut, strOut, 3*simd);
}

extern "C" ulong Btrfsst_compress(fsst_encoder_t *encoder,
                                    ulong nlines,
                                    const ulong lenIn[],
                                    const u8 *strIn[],
                                    ulong size,
                                    u8 *output,
                                    ulong *lenOut,
                                    u8 *strOut[],
                                    const fsst_options_t* optp) {
   fsst_options_t opt = optp ? *optp : fsst_options_t{0};
   if (opt.flags & FSST_OPT_DP_ENCODE) {
      return compressBulkDP(*((Encoder*) encoder)->symbolMap, nlines, lenIn, strIn, size, output, lenOut, strOut);
   }

   ulong totLen = accumulate(lenIn, lenIn+nlines, 0);
   int simd = totLen > nlines*12 && (nlines > 64 || totLen > (ulong) 1<<15);
   return _compressAuto((Encoder*) encoder, nlines, lenIn, strIn, size, output, lenOut, strOut, 3*simd);
}

/* deallocate encoder */
extern "C" void fsst_destroy(fsst_encoder_t* encoder) {
  Encoder *e = (Encoder*) encoder; 
   delete e;
}

/* very lazy implementation relying on export and import */
extern "C" fsst_decoder_t fsst_decoder(fsst_encoder_t *encoder) {
   u8 buf[sizeof(fsst_decoder_t)];
   u32 cnt1 = fsst_export(encoder, buf);
   fsst_decoder_t decoder;
   u32 cnt2 = fsst_import(&decoder, buf);
   assert(cnt1 == cnt2); (void) cnt1; (void) cnt2; 
   return decoder;
}
