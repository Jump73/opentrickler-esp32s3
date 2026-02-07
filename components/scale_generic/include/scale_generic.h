#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void scale_generic_init(void);
float scale_generic_get_grams(void);
bool scale_generic_is_stable(void);
void scale_generic_tare(void);
void scale_generic_feed_event(float grams_per_second);

#ifdef __cplusplus
}
#endif
