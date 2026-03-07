#include "flow_model.h"
#include "profile.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "flow_model";

#define NVS_NAMESPACE   "flow_model"
#define MAX_PROFILES    8
#define EMA_ALPHA       0.3f
#define HUBER_DELTA     0.5f   // max residual influence per EMA update (gn/s)

// ---------------------------------------------------------------------------
// Default speed bins (fixed, not adaptive)
// ---------------------------------------------------------------------------
static const float coarse_bins[FLOW_TABLE_POINTS] = {
    0.1f, 0.2f, 0.4f, 0.7f, 1.0f, 1.5f,
    2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f
};

static const float fine_bins[FLOW_TABLE_POINTS] = {
    0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.7f,
    1.0f,  1.3f, 1.6f, 2.0f, 2.3f, 3.0f
};

// ---------------------------------------------------------------------------
// Recording buffer (internal, not exposed in header)
// ---------------------------------------------------------------------------
typedef struct {
    flow_sample_t samples[FLOW_RECORD_MAX];
    uint16_t count;
    uint32_t start_tick;
    uint32_t stop_tick;
    uint8_t  motor;         // 0 = coarse, 1 = fine
    bool     recording;
} flow_recording_t;

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
static flow_recording_t s_recording;
static flow_model_t     s_models[MAX_PROFILES];

// Shadow model used during autotune freeze
static bool           s_frozen         = false;
static uint8_t        s_frozen_profile = 0xFF;
static flow_model_t   s_model_shadow;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static const float *get_bins_for_motor(uint8_t motor)
{
    return (motor == 0) ? coarse_bins : fine_bins;
}

static flow_model_single_t *get_single_model(uint8_t profile_idx, uint8_t motor)
{
    if (profile_idx >= MAX_PROFILES) return NULL;
    return (motor == 0) ? &s_models[profile_idx].coarse : &s_models[profile_idx].fine;
}

static flow_model_single_t *get_single_model_from(flow_model_t *base, uint8_t motor)
{
    return (motor == 0) ? &base->coarse : &base->fine;
}

static int find_nearest_bin(const float *bins, int n, float speed)
{
    int best = 0;
    float best_dist = fabsf(speed - bins[0]);
    for (int i = 1; i < n; i++) {
        float d = fabsf(speed - bins[i]);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

// Simple insertion sort for small arrays (used for median)
static void sort_floats(float *arr, int n)
{
    for (int i = 1; i < n; i++) {
        float key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static void init_model_bins(flow_model_single_t *m, const float *bins)
{
    m->num_points = FLOW_TABLE_POINTS;
    m->transport_delay_ms = 0.0f;
    m->inertia_factor_s = 0.0f;
    m->last_quality = 0.0f;
    for (int i = 0; i < FLOW_TABLE_POINTS; i++) {
        m->points[i].speed_rps = bins[i];
        m->points[i].flow_rate_gn_s = 0.0f;
        m->points[i].sample_count = 0;
    }
}

// ---------------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------------
esp_err_t flow_model_save(uint8_t profile_idx)
{
    if (profile_idx >= MAX_PROFILES) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    char key[8];
    snprintf(key, sizeof(key), "fm_%d", profile_idx);

    s_models[profile_idx].version = FLOW_MODEL_VERSION;
    ret = nvs_set_blob(handle, key, &s_models[profile_idx], sizeof(flow_model_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved flow model for profile %d", profile_idx);
    } else {
        ESP_LOGE(TAG, "Failed to save flow model: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t flow_model_reset(uint8_t profile_idx)
{
    if (profile_idx >= MAX_PROFILES) return ESP_ERR_INVALID_ARG;

    init_model_bins(&s_models[profile_idx].coarse, coarse_bins);
    init_model_bins(&s_models[profile_idx].fine, fine_bins);
    s_models[profile_idx].coarse.transport_delay_ms   = 0.0f;
    s_models[profile_idx].coarse.inertia_factor_s     = 0.0f;
    s_models[profile_idx].coarse.inertia_overshoot_gn = 0.0f;
    s_models[profile_idx].coarse.last_quality         = 0.0f;
    s_models[profile_idx].fine.transport_delay_ms     = 0.0f;
    s_models[profile_idx].fine.inertia_factor_s       = 0.0f;
    s_models[profile_idx].fine.inertia_overshoot_gn   = 0.0f;
    s_models[profile_idx].fine.last_quality           = 0.0f;
    s_models[profile_idx].version = FLOW_MODEL_VERSION;

    ESP_LOGI(TAG, "Flow model reset for profile %d", profile_idx);
    return flow_model_save(profile_idx);
}

esp_err_t flow_model_load(uint8_t profile_idx)
{
    if (profile_idx >= MAX_PROFILES) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;  // No saved data — not an error
    }

    char key[8];
    snprintf(key, sizeof(key), "fm_%d", profile_idx);

    size_t size = sizeof(flow_model_t);
    ret = nvs_get_blob(handle, key, &s_models[profile_idx], &size);
    nvs_close(handle);

    if (ret == ESP_OK && s_models[profile_idx].version == FLOW_MODEL_VERSION) {
        ESP_LOGI(TAG, "Loaded flow model for profile %d (coarse: %d pts, fine: %d pts)",
                 profile_idx,
                 s_models[profile_idx].coarse.num_points,
                 s_models[profile_idx].fine.num_points);
    } else {
        // Initialize with empty bins
        init_model_bins(&s_models[profile_idx].coarse, coarse_bins);
        init_model_bins(&s_models[profile_idx].fine, fine_bins);
        s_models[profile_idx].version = FLOW_MODEL_VERSION;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
esp_err_t flow_model_init(void)
{
    memset(&s_recording, 0, sizeof(s_recording));
    memset(s_models, 0, sizeof(s_models));

    // Initialize all profiles with default bins
    for (int i = 0; i < MAX_PROFILES; i++) {
        init_model_bins(&s_models[i].coarse, coarse_bins);
        init_model_bins(&s_models[i].fine, fine_bins);
        s_models[i].version = FLOW_MODEL_VERSION;
    }

    // Try to load current profile's model
    uint16_t idx = profile_get_selected_idx();
    flow_model_load(idx);

    ESP_LOGI(TAG, "Flow model initialized (profile %d)", idx);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------
void flow_model_record_start(uint8_t motor)
{
    s_recording.count = 0;
    s_recording.motor = motor;
    s_recording.start_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_recording.stop_tick = 0;
    s_recording.recording = true;
}

void flow_model_record_sample(float speed_rps, float weight_gn)
{
    if (!s_recording.recording) return;
    if (s_recording.count >= FLOW_RECORD_MAX) return;

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_recording.samples[s_recording.count++] = (flow_sample_t){
        .timestamp_ms = now_ms - s_recording.start_tick,
        .speed_rps = speed_rps,
        .weight_gn = weight_gn,
    };
}

void flow_model_record_stop(void)
{
    if (!s_recording.recording) return;
    // Mark motor stop time but keep recording — post-stop samples
    // are needed for inertia calculation (weight settling after motor off).
    // Call record_sample(0.0f, weight) after stop to capture settling.
    // Recording ends when record_start() or analyze_and_update() is called.
    s_recording.stop_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

// Temporary per-bin accumulator (static to avoid stack overflow in small tasks)
#define BIN_OBS_MAX 64

typedef struct {
    float values[BIN_OBS_MAX];
    int count;
} bin_accum_t;

static bin_accum_t s_bin_obs[FLOW_TABLE_POINTS];

esp_err_t flow_model_analyze_and_update(uint8_t profile_idx)
{
    // End recording if still active
    s_recording.recording = false;

    if (profile_idx >= MAX_PROFILES) return ESP_ERR_INVALID_ARG;
    if (s_recording.count < 10) {
        ESP_LOGW(TAG, "Recording too short (%d samples), skipping analysis", s_recording.count);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t motor = s_recording.motor;
    const float *bins = get_bins_for_motor(motor);
    flow_model_t *target = (s_frozen && s_frozen_profile == profile_idx)
                           ? &s_model_shadow
                           : &s_models[profile_idx];
    flow_model_single_t *model = get_single_model_from(target, motor);
    if (!model) return ESP_ERR_INVALID_ARG;

    const flow_sample_t *s = s_recording.samples;
    int n = s_recording.count;

    // Per-bin observation accumulators (static, zeroed each call)
    memset(s_bin_obs, 0, sizeof(s_bin_obs));

    // Step 1-3: Compute flow rates, filter, assign to bins
    float start_weight = s[0].weight_gn;
    int valid_obs = 0;
    int total_candidates = 0;
    int rejected = 0;

    // Spin-up rejection threshold: first 100ms after motor start
    const uint32_t SPINUP_MS = 100;

    for (int i = 0; i < n - 1; i++) {
        float dt = (float)(s[i + 1].timestamp_ms - s[i].timestamp_ms) / 1000.0f;
        float dw = s[i + 1].weight_gn - s[i].weight_gn;
        float avg_speed = (s[i].speed_rps + s[i + 1].speed_rps) * 0.5f;
        float speed_change = fabsf(s[i + 1].speed_rps - s[i].speed_rps);

        if (avg_speed < 0.01f) continue;    // Motor essentially stopped (not a candidate)
        total_candidates++;

        // Basic filters
        if (dt < 0.005f) { rejected++; continue; }           // Too close in time
        if (dw < 0.0f) { rejected++; continue; }             // Weight decreased (noise)

        // Extended filters (Phase 1b)
        if (s[i].timestamp_ms < SPINUP_MS) { rejected++; continue; }  // Spin-up phase
        if (speed_change > 0.1f) { rejected++; continue; }            // Non-steady speed

        // Braking filter: speed decreasing → in-flight powder contaminates bin
        if (i > 0 && s[i].speed_rps < s[i - 1].speed_rps - 0.02f) { rejected++; continue; }

        float flow_rate = dw / dt;
        if (flow_rate > 50.0f) { rejected++; continue; }     // Physically impossible

        // Find nearest bin
        int bin = find_nearest_bin(bins, FLOW_TABLE_POINTS, avg_speed);

        // Check bin proximity (within half the distance to neighbors)
        float bin_half_width;
        if (bin == 0) {
            bin_half_width = (bins[1] - bins[0]) * 0.5f;
        } else if (bin == FLOW_TABLE_POINTS - 1) {
            bin_half_width = (bins[FLOW_TABLE_POINTS - 1] - bins[FLOW_TABLE_POINTS - 2]) * 0.5f;
        } else {
            bin_half_width = fminf(bins[bin] - bins[bin - 1], bins[bin + 1] - bins[bin]) * 0.5f;
        }

        if (fabsf(avg_speed - bins[bin]) > bin_half_width) { rejected++; continue; }

        // Add to bin accumulator
        if (s_bin_obs[bin].count < BIN_OBS_MAX) {
            s_bin_obs[bin].values[s_bin_obs[bin].count++] = flow_rate;
            valid_obs++;
        }
    }

    // Compute quality score (0..1) for this dispense
    // Measures how "clean" the data was: low rejection ratio + low scale noise
    float quality = 1.0f;
    if (total_candidates > 0) {
        quality -= 0.3f * ((float)rejected / (float)total_candidates);
    }
    // Settling noise: RMS of weight differences in post-stop samples (speed=0)
    float settling_rms = 0.0f;
    int settling_count = 0;
    for (int i = 1; i < n; i++) {
        if (s[i].speed_rps < 0.01f && s[i - 1].speed_rps < 0.01f) {
            float dw = s[i].weight_gn - s[i - 1].weight_gn;
            settling_rms += dw * dw;
            settling_count++;
        }
    }
    if (settling_count > 0) {
        settling_rms = sqrtf(settling_rms / (float)settling_count);
        // Penalize if noise > 0.04 gn RMS (threshold for noisy scale)
        float noise_penalty = settling_rms / 0.04f;
        if (noise_penalty > 1.0f) noise_penalty = 1.0f;
        quality -= 0.3f * noise_penalty;
    }
    if (quality < 0.05f) quality = 0.05f;
    if (quality > 1.0f) quality = 1.0f;
    model->last_quality = quality;

    ESP_LOGI(TAG, "Analysis: %d samples, %d candidates, %d rejected, %d valid, quality=%.2f, motor=%s",
             n, total_candidates, rejected, valid_obs, quality, motor == 0 ? "COARSE" : "FINE");

    // Step 4-6: Compute median per bin and quality-weighted EMA merge
    int bins_updated = 0;
    for (int b = 0; b < FLOW_TABLE_POINTS; b++) {
        if (s_bin_obs[b].count < 3) continue;  // Need at least 3 observations

        // Compute median
        sort_floats(s_bin_obs[b].values, s_bin_obs[b].count);
        float median = s_bin_obs[b].values[s_bin_obs[b].count / 2];

        // Quality-weighted alpha: bad dispenses barely affect the model
        float obs_factor = (float)s_bin_obs[b].count / 10.0f;
        if (obs_factor < 0.1f) obs_factor = 0.1f;
        if (obs_factor > 1.0f) obs_factor = 1.0f;
        float alpha = EMA_ALPHA * quality * obs_factor;

        flow_point_t *pt = &model->points[b];
        if (pt->sample_count == 0) {
            // First observation — take directly
            pt->flow_rate_gn_s = median;
        } else {
            // Huber-clipped quality-weighted EMA merge:
            // limits influence of single anomalous dispense to HUBER_DELTA
            float r = median - pt->flow_rate_gn_s;
            float g = (fabsf(r) <= HUBER_DELTA) ? r : (HUBER_DELTA * (r > 0.0f ? 1.0f : -1.0f));
            pt->flow_rate_gn_s += alpha * g;
        }
        pt->sample_count++;
        bins_updated++;

        ESP_LOGI(TAG, "  bin[%d] speed=%.2f: flow=%.4f gn/s (median=%.4f, n=%d, alpha=%.3f, total=%d)",
                 b, pt->speed_rps, pt->flow_rate_gn_s, median,
                 s_bin_obs[b].count, alpha, pt->sample_count);
    }

    // Step 7: Transport delay (quality-weighted EMA)
    float delay_ms = 0.0f;
    for (int i = 1; i < n; i++) {
        if (s[i].weight_gn > start_weight + 0.02f) {
            delay_ms = (float)s[i].timestamp_ms;
            break;
        }
    }
    if (delay_ms > 0.0f) {
        if (model->transport_delay_ms < 1.0f) {
            model->transport_delay_ms = delay_ms;
        } else {
            float delay_alpha = EMA_ALPHA * quality;
            model->transport_delay_ms = (1.0f - delay_alpha) * model->transport_delay_ms + delay_alpha * delay_ms;
        }
        ESP_LOGI(TAG, "  transport_delay=%.1f ms", model->transport_delay_ms);
    }

    // Step 8: Inertia (weight gained after motor stop, quality-weighted EMA)
    if (s_recording.stop_tick > 0) {
        uint32_t stop_rel_ms = s_recording.stop_tick - s_recording.start_tick;

        // Find sample closest to stop moment
        int stop_idx = -1;
        for (int i = 0; i < n; i++) {
            if (s[i].timestamp_ms >= stop_rel_ms) {
                stop_idx = i;
                break;
            }
        }

        if (stop_idx >= 0 && stop_idx < n - 5) {
            float weight_at_stop = s[stop_idx].weight_gn;
            float final_weight = s[n - 1].weight_gn;
            float overshoot_gn = final_weight - weight_at_stop;

            // Estimate flow rate just before stop (last 5 samples before stop)
            float pre_stop_flow = 0.0f;
            int pre_count = 0;
            for (int i = fmaxf(1, stop_idx - 5); i < stop_idx; i++) {
                float dt = (float)(s[i].timestamp_ms - s[i - 1].timestamp_ms) / 1000.0f;
                float dw = s[i].weight_gn - s[i - 1].weight_gn;
                if (dt > 0.005f && dw > 0.0f) {
                    pre_stop_flow += dw / dt;
                    pre_count++;
                }
            }

            if (pre_count > 0 && pre_stop_flow > 0.0f) {
                pre_stop_flow /= (float)pre_count;
                float inertia_s = overshoot_gn / pre_stop_flow;

                if (inertia_s > 0.0f && inertia_s < 5.0f) {
                    float inertia_alpha = EMA_ALPHA * quality;
                    if (model->inertia_factor_s < 0.001f) {
                        model->inertia_factor_s = inertia_s;
                        model->inertia_overshoot_gn = overshoot_gn;
                    } else {
                        model->inertia_factor_s = (1.0f - inertia_alpha) * model->inertia_factor_s + inertia_alpha * inertia_s;
                        model->inertia_overshoot_gn = (1.0f - inertia_alpha) * model->inertia_overshoot_gn + inertia_alpha * overshoot_gn;
                    }
                    ESP_LOGI(TAG, "  inertia=%.3f s, overshoot_gn=%.3f gn (pre_flow=%.3f gn/s)",
                             model->inertia_factor_s, model->inertia_overshoot_gn, pre_stop_flow);
                }
            }
        }
    }

    // Save to NVS (skip when frozen — shadow is not persisted to NVS)
    if (bins_updated > 0 && !(s_frozen && s_frozen_profile == profile_idx)) {
        flow_model_save(profile_idx);
    }

    ESP_LOGI(TAG, "Updated %d bins for profile %d %s motor",
             bins_updated, profile_idx, motor == 0 ? "coarse" : "fine");

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Query functions
// ---------------------------------------------------------------------------
float flow_model_get_flow_rate(uint8_t profile_idx, uint8_t motor, float speed_rps)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    if (!m || m->num_points == 0) return 0.0f;

    // Find surrounding points for interpolation
    const flow_point_t *pts = m->points;
    int n = m->num_points;

    // Below lowest bin
    if (speed_rps <= pts[0].speed_rps) {
        return pts[0].flow_rate_gn_s;
    }
    // Above highest bin
    if (speed_rps >= pts[n - 1].speed_rps) {
        return pts[n - 1].flow_rate_gn_s;
    }

    // Linear interpolation between two surrounding bins with data
    for (int i = 0; i < n - 1; i++) {
        if (speed_rps >= pts[i].speed_rps && speed_rps <= pts[i + 1].speed_rps) {
            if (pts[i].sample_count == 0 && pts[i + 1].sample_count == 0) return 0.0f;
            float t = (speed_rps - pts[i].speed_rps) / (pts[i + 1].speed_rps - pts[i].speed_rps);
            return pts[i].flow_rate_gn_s + t * (pts[i + 1].flow_rate_gn_s - pts[i].flow_rate_gn_s);
        }
    }
    return 0.0f;
}

float flow_model_get_speed_for_rate(uint8_t profile_idx, uint8_t motor, float desired_gn_s)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    if (!m || m->num_points == 0 || desired_gn_s <= 0.0f) return 0.0f;

    const flow_point_t *pts = m->points;
    int n = m->num_points;

    // Find two points that bracket the desired flow rate
    for (int i = 0; i < n - 1; i++) {
        if (pts[i].sample_count == 0 || pts[i + 1].sample_count == 0) continue;

        float f0 = pts[i].flow_rate_gn_s;
        float f1 = pts[i + 1].flow_rate_gn_s;

        if ((desired_gn_s >= f0 && desired_gn_s <= f1) ||
            (desired_gn_s <= f0 && desired_gn_s >= f1)) {
            if (fabsf(f1 - f0) < 0.0001f) continue;
            float t = (desired_gn_s - f0) / (f1 - f0);
            return pts[i].speed_rps + t * (pts[i + 1].speed_rps - pts[i].speed_rps);
        }
    }

    // If desired rate is higher than anything in the model, return max speed
    if (desired_gn_s > 0.0f) {
        for (int i = n - 1; i >= 0; i--) {
            if (pts[i].sample_count > 0 && pts[i].flow_rate_gn_s > 0.0f) {
                return pts[i].speed_rps;
            }
        }
    }
    return 0.0f;
}

float flow_model_get_transport_delay(uint8_t profile_idx, uint8_t motor)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    return m ? m->transport_delay_ms : 0.0f;
}

float flow_model_get_inertia(uint8_t profile_idx, uint8_t motor)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    return m ? m->inertia_factor_s : 0.0f;
}

float flow_model_get_inertia_overshoot(uint8_t profile_idx, uint8_t motor)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    return m ? m->inertia_overshoot_gn : 0.0f;
}

bool flow_model_is_trusted(uint8_t profile_idx, uint8_t motor)
{
    flow_model_single_t *m = get_single_model(profile_idx, motor);
    if (!m) return false;

    // Model is trusted when >=3 bins have sample_count >= 5
    int trusted_bins = 0;
    for (int i = 0; i < m->num_points; i++) {
        if (m->points[i].sample_count >= 5) {
            trusted_bins++;
        }
    }
    return trusted_bins >= 3;
}

esp_err_t flow_model_get(uint8_t profile_idx, flow_model_t *out)
{
    if (profile_idx >= MAX_PROFILES || !out) return ESP_ERR_INVALID_ARG;
    // Return shadow when frozen so callers (e.g. autotune quality gate) see the
    // model as it evolves during the current autotune session, not the stale
    // live snapshot that was taken at freeze time.
    if (s_frozen && s_frozen_profile == profile_idx)
        *out = s_model_shadow;
    else
        *out = s_models[profile_idx];
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Freeze / shadow merge
// ---------------------------------------------------------------------------

void flow_model_freeze(uint8_t profile_idx)
{
    if (profile_idx >= MAX_PROFILES) return;
    s_model_shadow   = s_models[profile_idx];   // copy live → shadow
    s_frozen_profile = profile_idx;
    s_frozen         = true;
    ESP_LOGI(TAG, "Flow model frozen for profile %d", profile_idx);
}

void flow_model_unfreeze(void)
{
    if (!s_frozen) return;
    s_frozen         = false;
    s_frozen_profile = 0xFF;
    ESP_LOGI(TAG, "Flow model unfrozen (shadow discarded)");
}

esp_err_t flow_model_shadow_merge(void)
{
    if (!s_frozen || s_frozen_profile >= MAX_PROFILES) {
        return ESP_ERR_INVALID_STATE;
    }
    s_models[s_frozen_profile] = s_model_shadow;    // apply shadow → live
    esp_err_t ret = flow_model_save(s_frozen_profile);
    ESP_LOGI(TAG, "Flow model shadow merged into profile %d", s_frozen_profile);
    flow_model_unfreeze();
    return ret;
}
