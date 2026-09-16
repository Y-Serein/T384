#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t384_radiometry.h"

static uint16_t kt[T384_RADIOMETRY_KTBT_V2_LENGTH];
static int16_t bt[T384_RADIOMETRY_KTBT_V2_LENGTH];
static uint16_t nuc_t[T384_RADIOMETRY_NUC_T_LEGACY_LENGTH];

static t384_radiometry_profile_t make_profile(void)
{
    t384_radiometry_profile_t profile = {
        .schema = T384_RADIOMETRY_PROFILE_SCHEMA,
        .product = T384_RADIOMETRY_PRODUCT_WN384,
        .calibration_version = T384_RADIOMETRY_CALIBRATION_V2,
        .gain = T384_RADIOMETRY_GAIN_HIGH,
        .kt_length = T384_RADIOMETRY_KTBT_V2_LENGTH,
        .bt_length = T384_RADIOMETRY_KTBT_V2_LENGTH,
        .nuc_t_length = T384_RADIOMETRY_NUC_T_LEGACY_LENGTH,
        .kt_q14 = kt,
        .bt = bt,
        .nuc_t_kelvin_x16 = nuc_t,
        .calibration_identity = 0x38400001u,
        .calibration_epoch = 7u,
    };
    return profile;
}

static t384_radiometry_frame_state_t make_state(void)
{
    t384_radiometry_frame_state_t state = {
        .flags = T384_RADIOMETRY_STATE_READY_MASK,
        .gain = T384_RADIOMETRY_GAIN_HIGH,
        .ktbt_index = 0u,
        .vtemp_raw = 100u,
        .calibration_identity = 0x38400001u,
        .calibration_epoch = 7u,
        .frame_sequence = 42u,
    };
    return state;
}

int main(void)
{
    memset(kt, 0, sizeof(kt));
    memset(bt, 0, sizeof(bt));
    memset(nuc_t, 0, sizeof(nuc_t));
    kt[0] = 16384u;
    nuc_t[1000] = 4800u;

    t384_radiometry_profile_t profile = make_profile();
    t384_radiometry_frame_state_t state = make_state();
    t384_radiometry_result_t result;
    uint16_t y14 = 0u;

    assert(t384_radiometry_y16_to_y14(42072u, &y14) ==
           T384_RADIOMETRY_OK);
    assert(y14 == 10518u);
    assert(t384_radiometry_y16_to_y14(UINT16_MAX, &y14) ==
           T384_RADIOMETRY_OK);
    assert(y14 == T384_RADIOMETRY_NUC14_MAX);

    assert(t384_radiometry_validate_profile(&profile) ==
           T384_RADIOMETRY_OK);
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_OK);
    assert(result.corrected_nuc == 1000u);
    assert(result.temperature_kelvin_x16 == 4800u);
    assert(result.temperature_c_x100 == 2685);
    assert(result.frame_sequence == 42u);

    state.calibration_identity ^= 1u;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_STATE_NOT_READY);
    state.calibration_identity = profile.calibration_identity;

    bt[0] = -2000;
    nuc_t[0] = 4370u;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_OK);
    assert(result.corrected_nuc == 0u);

    bt[0] = 8000;
    nuc_t[T384_RADIOMETRY_NUC_T_LEGACY_LENGTH - 1u] = 4800u;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_OK);
    assert(result.corrected_nuc ==
           T384_RADIOMETRY_NUC_T_LEGACY_LENGTH - 1u);

    assert(t384_radiometry_measure_nuc14(
               &profile, &state, T384_RADIOMETRY_NUC14_MAX + 1u, &result) ==
           T384_RADIOMETRY_INPUT_OUT_OF_RANGE);

    state.flags &= (uint16_t)~T384_RADIOMETRY_STATE_FFC_STABLE;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_STATE_NOT_READY);
    state = make_state();
    state.gain = T384_RADIOMETRY_GAIN_LOW;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_GAIN_MISMATCH);
    state = make_state();
    state.ktbt_index = profile.kt_length;
    assert(t384_radiometry_measure_nuc14(&profile, &state, 1000u, &result) ==
           T384_RADIOMETRY_INDEX_OUT_OF_RANGE);

    profile.bt_length -= 1u;
    assert(t384_radiometry_validate_profile(&profile) ==
           T384_RADIOMETRY_INVALID_TABLE_LENGTH);

    puts("T384 radiometry core smoke passed");
    return 0;
}
