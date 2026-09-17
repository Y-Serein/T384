#include "t384_radiometry.h"

#include <stddef.h>
#include <string.h>

static bool supported_ktbt_length(uint16_t version, uint16_t length)
{
    if (version == T384_RADIOMETRY_CALIBRATION_V2) {
        return length == T384_RADIOMETRY_KTBT_V2_LENGTH;
    }
    if (version == T384_RADIOMETRY_CALIBRATION_V3) {
        return length == T384_RADIOMETRY_KTBT_V3_LENGTH;
    }
    return false;
}

static bool supported_nuc_t_length(uint16_t length)
{
    return length == T384_RADIOMETRY_NUC_T_LEGACY_LENGTH ||
           length == T384_RADIOMETRY_NUC_T_ADVANCED_LENGTH;
}

t384_radiometry_status_t t384_radiometry_validate_profile(
    const t384_radiometry_profile_t *profile)
{
    if (profile == NULL || profile->kt_q14 == NULL || profile->bt == NULL ||
        profile->nuc_t_kelvin_x16 == NULL || profile->calibration_identity == 0u) {
        return T384_RADIOMETRY_INVALID_ARGUMENT;
    }
    if (profile->schema != T384_RADIOMETRY_PROFILE_SCHEMA ||
        (profile->product != T384_RADIOMETRY_PRODUCT_WN384 &&
         profile->product != T384_RADIOMETRY_PRODUCT_WN640) ||
        (profile->gain != T384_RADIOMETRY_GAIN_HIGH &&
         profile->gain != T384_RADIOMETRY_GAIN_LOW)) {
        return T384_RADIOMETRY_UNSUPPORTED_PROFILE;
    }
    if (profile->kt_length != profile->bt_length ||
        !supported_ktbt_length(profile->calibration_version,
                               profile->kt_length) ||
        !supported_nuc_t_length(profile->nuc_t_length)) {
        return T384_RADIOMETRY_INVALID_TABLE_LENGTH;
    }
    return T384_RADIOMETRY_OK;
}

t384_radiometry_status_t t384_radiometry_y16_to_y14(uint16_t y16, uint16_t *y14)
{
    if (y14 == NULL) {
        return T384_RADIOMETRY_INVALID_ARGUMENT;
    }
    *y14 = (uint16_t)(y16 >> 2);
    return T384_RADIOMETRY_OK;
}

static int32_t kelvin_x16_to_celsius_x100(uint16_t kelvin_x16)
{
    const int32_t scaled = (int32_t)kelvin_x16 * 25;
    return (scaled + 2) / 4 - 27315;
}

t384_radiometry_status_t t384_radiometry_measure_nuc14(
    const t384_radiometry_profile_t *profile,
    const t384_radiometry_frame_state_t *state,
    uint16_t input_nuc14,
    t384_radiometry_result_t *result)
{
    if (state == NULL || result == NULL) {
        return T384_RADIOMETRY_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));

    t384_radiometry_status_t status =
        t384_radiometry_validate_profile(profile);
    if (status != T384_RADIOMETRY_OK) {
        return status;
    }
    if ((state->flags & T384_RADIOMETRY_STATE_READY_MASK) !=
            T384_RADIOMETRY_STATE_READY_MASK ||
        state->calibration_identity == 0u ||
        state->calibration_identity != profile->calibration_identity ||
        state->calibration_epoch != profile->calibration_epoch) {
        return T384_RADIOMETRY_STATE_NOT_READY;
    }
    if (state->gain != profile->gain) {
        return T384_RADIOMETRY_GAIN_MISMATCH;
    }
    if (state->ktbt_index >= profile->kt_length) {
        return T384_RADIOMETRY_INDEX_OUT_OF_RANGE;
    }
    if (input_nuc14 > T384_RADIOMETRY_NUC14_MAX) {
        return T384_RADIOMETRY_INPUT_OUT_OF_RANGE;
    }

    const uint32_t index = state->ktbt_index;
    const int64_t product =
        (int64_t)profile->kt_q14[index] * (int64_t)input_nuc14;
    int64_t corrected =
        (int64_t)profile->bt[index] + ((product + 8192) >> 14);
    if (corrected < 0) {
        corrected = 0;
    } else if (corrected >= profile->nuc_t_length) {
        corrected = profile->nuc_t_length - 1u;
    }

    const uint16_t corrected_nuc = (uint16_t)corrected;
    const uint16_t kelvin_x16 =
        profile->nuc_t_kelvin_x16[corrected_nuc];
    result->input_nuc14 = input_nuc14;
    result->corrected_nuc = corrected_nuc;
    result->temperature_kelvin_x16 = kelvin_x16;
    result->temperature_c_x100 = kelvin_x16_to_celsius_x100(kelvin_x16);
    result->ktbt_index = state->ktbt_index;
    result->gain = state->gain;
    result->calibration_epoch = state->calibration_epoch;
    result->frame_sequence = state->frame_sequence;
    return T384_RADIOMETRY_OK;
}

const char *t384_radiometry_status_name(t384_radiometry_status_t status)
{
    switch (status) {
    case T384_RADIOMETRY_OK:
        return "ok";
    case T384_RADIOMETRY_INVALID_ARGUMENT:
        return "invalid-argument";
    case T384_RADIOMETRY_UNSUPPORTED_PROFILE:
        return "unsupported-profile";
    case T384_RADIOMETRY_INVALID_TABLE_LENGTH:
        return "invalid-table-length";
    case T384_RADIOMETRY_STATE_NOT_READY:
        return "state-not-ready";
    case T384_RADIOMETRY_GAIN_MISMATCH:
        return "gain-mismatch";
    case T384_RADIOMETRY_INDEX_OUT_OF_RANGE:
        return "index-out-of-range";
    case T384_RADIOMETRY_INPUT_OUT_OF_RANGE:
        return "input-out-of-range";
    default:
        return "unknown";
    }
}
