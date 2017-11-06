#include "dtoa/strtod-mode.c"
#include "dtoa/dtoa-aux.c"

static int dtoa_round_trip(double x, int mode)
{
  char *s = edge.str + edge.head;
  s[0] = '.';
  sprintf(&s[edge.len + 1], "E%d", edge.dec);
  EPRINT("\nedge = %s\n" , s);
  if (mode) fesetround(mode<0 ? FE_DOWNWARD : FE_UPWARD);
  int roundtrip = (fabs(x) == strtod_fast(s, NULL));
  if (mode) fesetround(FE_TONEAREST);
  return roundtrip;
}

static struct dtoa_case *dtoa_shortest
(int64_t m, double frac, int dec, double ulp, double x, int mode)
{
  if (ulp >= 5) {           // ulp < 5 imply j-i < 10
    uint64_t m0 = m, scale = 1;
    do {scale *= 10; dec++;} while ((ulp *= 0.1) >= 5);
    m /= scale;
    frac = (frac + (m0 - m * scale)) / scale;
  }
  else if (ulp < 0.5) {     // ulp >= 0.5 imply j >= i
    int d = (int) (frac *= 10);
    m = m * 10 + d;
    ulp *= 10; dec--;
    frac -= d;
  }
  if (mode) frac += (mode>0 ? ulp : -ulp) * (1+2*ERR);

  int i = lrint(frac - (ulp - 0.5));
  int j = lrint(frac + (ulp - 0.5));
  EPRINT("\nmantissa => %I64u + %.17g", m, frac);
  EPRINT("\nsafe ulp => %.17g\n", ulp);

  ulp *= 1 + 4*ERR;         // ulp for edges
  if (dtoa_safe(m + j, j - i, dec)) return &safe;

  if (safe.str[LAST]=='9' && frac+ulp >= j+1 && dtoa_edge('9'))
    if (dtoa_round_trip(x, mode)) return &edge;

  if (safe.str[0] == '1'  && frac-ulp <= i-1 && dtoa_edge('0'))
    if (dtoa_round_trip(x, mode)) return &edge;

  switch (mode) {
    case 0:                 // FE_TONEAREST
      safe.str[LAST] -= j - (frac > 0.5);
      if (fabs(frac - 0.5) < ERR) safe.head = 0;
      break;
    case -1:                // FE_TOWARDZERO
      safe.str[LAST] -= j - i;
      if (frac-ulp > i-1) break;
    case 1:                 // fall thru
      if (mode==1 && frac+ulp < j+1) break;
      edge = safe;          // edge may be better
      i = edge.str[LAST] += mode;
      if (abs(i - '5') == 5) break;
      if (dtoa_round_trip(x, mode)) return &edge;
  }
  return &safe;
}

static struct dtoa_case *dtoa_do_digit
(int64_t m, double frac, int dec, int digits, int mode)
{
  if (digits > 17) digits = 17;   // limit max digits
  safe.head = 0;
  safe.len = digits;

  int len=17;
  if (m < (int64_t) 1E16) len--;  // len of m digits
  dec += (len -= digits);         // scale m to digits

  if (len > 0) {                  // scale down mantissa
    if (frac > 0.5) {m++; frac--;}
    uint64_t scale = 1, m0 = m;
    for(; len>=2; len-=2) scale *= 100;
    if (len) scale *= 10;
    m /= scale;
    m0 -= m * scale;
    if (mode) {
      if (m0) m += (mode>0);
      else if (fabs(frac) < ERR) return &safe;
      else if ((frac < 0) == (mode < 0)) m += mode;
    } else {
      scale >>= 1;                // check half-way
      if      (m0 < scale) ;
      else if (m0 > scale) m++;
      else if (fabs(frac) < ERR) return &safe;
      else if (frac > 0) m++;     // frac to break tie
    }
  } else {
    if (len) {m*=10; frac*=10;}   // scale up 10x
    int d = lrint(frac);
    m += d;
    if (mode) {
      if (fabs(frac-d) < ERR) return &safe;
      else if ((frac < d) == (mode < 0)) m += mode;
    } else
      if (fabs(frac-d) > 0.5-ERR) return &safe;
  }
  dtoa_safe(m, 0, dec);
  return &safe;
}

char *dtoa_fast(double x, int digits, int *psgn, int *plen, int *pdec)
{
  union HexDouble u = {d:x};
  int i=0;
  int bexp = u.u >> 52;
  u.u &= ~0ULL >> 12;       // mantissa part only
  *psgn = bexp >> 11;       // save sign
  int pow2 = (u.u == 0);    // x is power of 2

  switch (bexp &= 0x7ff) {  // remove sign
    case 0:
      if (pow2) {           // only a zero
        *plen = *pdec = 1;
        return strcpy(safe.str, "0");
      }
      i = clzll(u.u) - 11;  // subnormal number
      u.u <<= i;            // treat it as normal
      bexp = -i;
      break;
    case 0x7ff:
      *plen = *pdec = 0;    // bad number
      return strcpy(safe.str, u.u ? "NaN" : "Inf");
    default:                // normal number
      u.u |= 1ULL<<52;      // add imply bits
      bexp--;
  }
  uint64_t m0 = u.u << 11;
  u.u += (uint64_t)(BIAS+54-i) << 52;

  unsigned n2 = m0 >> 32;   // top    32 bits
  unsigned n1 = m0;         // middle 32 bits
  unsigned n0 = 0;          // bottom 32 bits
  int dec = LAST;           // powers of 10
  int bexp0 = bexp;         // Goal: bexp-BIAS <= 56

  BIT96("rounded double");
  for(; bexp >BIAS+56    ; dec+=73) DIV_1E73(n2,n1,n0,bexp);
  for(; bexp<=BIAS+56-200; dec-=60) MUL_1E60(n2,n1,n0,bexp);
  for(; bexp<=BIAS+56- 84; dec-=25) MUL_1E25(n2,n1,n0,bexp);
  for(; bexp<=BIAS+56- 44; dec-=12) MUL_1E12(n2,n1,n0,bexp);

  int n = BIAS + 56 - bexp; // scale away 2^n
  dec -= i = n * 77 >> 8;   // 77/2^8 = 0.3008 ~= log2
  MUL32(n2,n1,n0,bexp, t[i], t_bexp[i]);
  BIT96("mantissa only");

  uint64_t m = ((uint64_t) n2<<32 | n1) >> (BIAS+64-bexp);
  double ulp = (1-2*ERR) / u.d * m;

  u.d = 1;                  // mantissa fractions after 1
  u.u |= ((uint64_t) n1<<32 | n0) << (bexp-BIAS-32) >> 12;

  int old, mode = 0;        // round-to-nearest
  switch(old=fegetround()) {
    case FE_DOWNWARD:   mode = *psgn * 2 - 1; break;
    case FE_UPWARD:     mode = 1 - *psgn * 2; break;
    case FE_TOWARDZERO: mode = -1; break;
  }
  if (digits < 0) digits=~digits, mode=-mode;
  if (old != FE_TONEAREST) {
    fesetround(FE_TONEAREST);
    if (pow2 && (pow2--, !digits) && mode<0) ulp *= 0.5;
  }

  struct dtoa_case *best;
  if (digits)    best = dtoa_do_digit(m, u.d-1, dec, digits, mode);
  else if (pow2) best = dtoa_boundary(m, u.d-1, dec, ulp);
  else           best = dtoa_shortest(m, u.d-1, dec, ulp, x, mode);

  if (!best->head) dtoa_hard(best, m0, bexp0-BIAS-64, mode);
  if (old != FE_TONEAREST) fesetround(old);

  char *s = best->str + best->head + 1;
  *pdec = best->dec;
  *plen = best->len;
  s[*plen] = '\0';
  EPRINT("best = %s\n\n", s);
  return s;
}
