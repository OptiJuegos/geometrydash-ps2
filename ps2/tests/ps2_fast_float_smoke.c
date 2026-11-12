#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../source/ps2_fast_float.h"

int main(void)
{
    /* Typical level, trigger and color metadata. Both old and new paths
     * should return the same float within one ULP on these inputs. */
    const char *values[] = {
        "0", "1", "-0.0", "0.75", "1.2", "2.0", "0.3",
        "-1969.92", "-1676.84", "345.34079", "573.481728",
        "100.0", "0.001", "1e-4", "2500.05", "-402.189"
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const float original = (float)atof(values[i]);
        const float optimized = gd_parse_level_float(values[i]);
        assert(isfinite(original) && isfinite(optimized));
        const float tolerance = fmaxf(fabsf(nextafterf(original, INFINITY) - original),
                                      fabsf(original - nextafterf(original, -INFINITY)));
        assert(fabsf(optimized - original) <= tolerance);
    }
    /* Same prefix as atof(), preserving the GD 'a'-delimited HSV tokens. */
    assert(gd_parse_level_float("0.5a1a0") == 0.5f);
    assert(gd_parse_level_float("1.25,2") == 1.25f);
    puts("PASS PS2 strtof parser: level floats and trigger/color prefixes");
    return 0;
}
