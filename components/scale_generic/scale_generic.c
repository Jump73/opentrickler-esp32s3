#include "scale_generic.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"

static const char *TAG = "SCALE_GEN";

/* Internal simulated state */
static float s_mass = 0.0f;
static bool  s_stable = true;

/* Model parameters */
#define STABILITY_THRESHOLD   0.02f
#define NOISE_LEVEL           0.005f

static float fake_noise(void)
{
    return ((float)(esp_random() % 1000) / 1000.0f - 0.5f) * NOISE_LEVEL;
}

void scale_generic_init(void)
{
    s_mass = 0.0f;
    s_stable = true;
    ESP_LOGI(TAG, "Generic scale simulator initialized");
}

float scale_generic_get_grams(void)
{
    return s_mass + fake_noise();
}

bool scale_generic_is_stable(void)
{
    return s_stable;
}

void scale_generic_tare(void)
{
    s_mass = 0.0f;
    s_stable = true;
    ESP_LOGI(TAG, "TARE executed (simulated)");
}

void scale_generic_feed_event(float grams_per_second)
{
    /* Very simple "powder flow" model */
    s_mass += grams_per_second * 0.1f; // called every ~100ms in tests => 0.1s

    /* During feeding, reading is unstable */
    s_stable = false;

    /* If feed rate is ~0, reading becomes stable */
    if (grams_per_second < STABILITY_THRESHOLD) {
        s_stable = true;
    }
}
