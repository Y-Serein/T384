#ifndef T384_RADIOMETRY_H
#define T384_RADIOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define T384_RADIOMETRY_PROFILE_SCHEMA 1u
#define T384_RADIOMETRY_NUC14_MAX 16383u
#define T384_RADIOMETRY_KTBT_V2_LENGTH 1021u
#define T384_RADIOMETRY_KTBT_V3_LENGTH 3601u
#define T384_RADIOMETRY_NUC_T_LEGACY_LENGTH 8192u
#define T384_RADIOMETRY_NUC_T_ADVANCED_LENGTH 16384u

#define T384_RADIOMETRY_STATE_INDEX_VALID 0x0001u
#define T384_RADIOMETRY_STATE_GAIN_VALID 0x0002u
#define T384_RADIOMETRY_STATE_FFC_STABLE 0x0004u
#define T384_RADIOMETRY_STATE_SNR_NUC_VALID 0x0008u
#define T384_RADIOMETRY_STATE_READY_MASK \
    (T384_RADIOMETRY_STATE_INDEX_VALID | \
     T384_RADIOMETRY_STATE_GAIN_VALID | \
     T384_RADIOMETRY_STATE_FFC_STABLE | \
     T384_RADIOMETRY_STATE_SNR_NUC_VALID)

typedef enum {
    T384_RADIOMETRY_PRODUCT_UNKNOWN = 0,
    T384_RADIOMETRY_PRODUCT_WN384 = 1,
    T384_RADIOMETRY_PRODUCT_WN640 = 2,
} t384_radiometry_product_t;

typedef enum {
    T384_RADIOMETRY_CALIBRATION_V2 = 2,
    T384_RADIOMETRY_CALIBRATION_V3 = 3,
} t384_radiometry_calibration_version_t;

typedef enum {
    T384_RADIOMETRY_GAIN_HIGH = 1,
    T384_RADIOMETRY_GAIN_LOW = 2,
} t384_radiometry_gain_t;

typedef enum {
    T384_RADIOMETRY_OK = 0,
    T384_RADIOMETRY_INVALID_ARGUMENT,
    T384_RADIOMETRY_UNSUPPORTED_PROFILE,
    T384_RADIOMETRY_INVALID_TABLE_LENGTH,
    T384_RADIOMETRY_STATE_NOT_READY,
    T384_RADIOMETRY_GAIN_MISMATCH,
    T384_RADIOMETRY_INDEX_OUT_OF_RANGE,
    T384_RADIOMETRY_INPUT_OUT_OF_RANGE,
} t384_radiometry_status_t;

/*
 * A profile contains one gain's immutable, already authenticated tables.
 * The loader owns the table storage and must validate file identity, byte
 * order, exact length and integrity before publishing this view.
 */
typedef struct {
    uint16_t schema;
    uint16_t product;
    uint16_t calibration_version;
    uint16_t gain;
    uint16_t kt_length;
    uint16_t bt_length;
    uint16_t nuc_t_length;
    const uint16_t *kt_q14;
    const int16_t *bt;
    const uint16_t *nuc_t_kelvin_x16;
    /* Truncated hash of PN/SN/FW/table manifest; zero is never publishable. */
    uint32_t calibration_identity;
    uint32_t calibration_epoch;
} t384_radiometry_profile_t;

/*
 * This state must describe the same physical frame as the NUC input.  The
 * source/state adapter derives ktbt_index from authoritative module metadata;
 * this core deliberately does not guess the vendor-specific Vtemp formula.
 */
typedef struct {
    uint16_t flags;
    uint16_t gain;
    uint16_t ktbt_index;
    uint16_t vtemp_raw;
    uint32_t calibration_identity;
    uint32_t calibration_epoch;
    uint32_t frame_sequence;
} t384_radiometry_frame_state_t;

typedef struct {
    uint16_t input_nuc14;
    uint16_t corrected_nuc;
    uint16_t temperature_kelvin_x16;
    int32_t temperature_c_x100;
    uint16_t ktbt_index;
    uint16_t gain;
    uint32_t calibration_epoch;
    uint32_t frame_sequence;
} t384_radiometry_result_t;

t384_radiometry_status_t t384_radiometry_validate_profile(
    const t384_radiometry_profile_t *profile);

/* Vendor-verified format boundary: Y16 is a 16-bit container, Y14 is the
 * 14-bit value consumed by the calibration/SNR APIs. */
t384_radiometry_status_t t384_radiometry_y16_to_y14(uint16_t y16,
                                                    uint16_t *y14);

/*
 * Input is NUC14 after the documented Y16-to-Y14/SNR normalization boundary,
 * never the current 16-bit DVP container value directly.
 */
t384_radiometry_status_t t384_radiometry_measure_nuc14(
    const t384_radiometry_profile_t *profile,
    const t384_radiometry_frame_state_t *state,
    uint16_t input_nuc14,
    t384_radiometry_result_t *result);

const char *t384_radiometry_status_name(t384_radiometry_status_t status);

#endif
