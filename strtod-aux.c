#include "mapm/dtoa-fast.h"
#include "mapm/mapm_clz.c"

static unsigned t[] = {   // normalized 10^0 to 10^12
  0x80000000, 0xa0000000, 0xc8000000, 0xfa000000,
  0x9c400000, 0xc3500000, 0xf4240000, 0x98968000,
  0xbebc2000, 0xee6b2800, 0x9502f900, 0xba43b740, 0xe8d4a510
};
static char t_bexp[] = {1,4,7,10,14,17,20,24,27,30,34,37,40};

// code to handle hard cases
#define ULP()           strtod_ulp(str, n2, n1, bexp)
#define FIX_0x400(r)    r += ULP() - 1
#define FIX_0x3ff(r)    r += ULP() + !!(n1&0x800)
#define FIX_0x7ff(r)    r += ULP() + !r

#if USE_MAPM == 0           // Default = GMP
#include "dtoa/strtod-gmp.c"
#else

#if USE_MAPM == 1
#include "mapm/m_apm.h"     // MAPM, link with libmapm.a
#else
#include "mapm/mapm_mul.c"  // grade school multiply only
#endif

#if USE_MAPM == 3           // 1 big strtod-fast.c
  #include "mapm/mapm_util.c"
  #include "mapm/mapm_utl2.c"
  #include "mapm/mapm_cnst.c"
  #include "mapm/mapm_stck.c"
  #include "mapm/mapm_set.c"
  #include "mapm/mapm_pwr2.c"
#endif

// Let b = half-way 54 bits estimate
// Let d = decimals from string
// strtod_ulp return sign(|d| - |b|)

static int strtod_ulp(const char *str, unsigned n2, unsigned n1, int bexp)
{
  M_APM d = M_get_stack_var();  // d = decimals from string
  m_apm_set_string(d, str);

  if (d->m_apm_exponent >= d->m_apm_datalength && bexp <= 91+BIAS) {
    M_restore_stack(1);         // FAST-PATH, d <= 0x1p91
    return 0;                   // |d-b| < 1, thus d = b
  }

  int old = fegetround();
  fesetround(FE_TONEAREST);
  M_APM b = M_get_stack_var();
  uint64_t m = (uint64_t) n2 << 22;
  m += ((n1>>9) + 1) >> 1;      // 54-bit half-way
  m_apm_set_uint64_t(b, m);
  m_apm_ishift(b, bexp-54-BIAS);
  fesetround(old);

  int sign = m_apm_compare_absolute(d, b);
  EPRINT("\tsign = %+d\n", sign);
  M_restore_stack(2);
  return sign;
}
#endif
