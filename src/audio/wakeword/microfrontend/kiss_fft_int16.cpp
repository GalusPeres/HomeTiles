#include <cstdint>

#include "kiss_fft_common.h"

#define FIXED_POINT 16
namespace hometiles_kissfft_fixed16 {
#include "kissfft/kiss_fft_impl.inc.h"
#include "kissfft/kiss_fftr_impl.inc.h"
}  // namespace hometiles_kissfft_fixed16
#undef FIXED_POINT
