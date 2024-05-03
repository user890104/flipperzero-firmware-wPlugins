#include "hormann_bisecur.h"

#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/toolbox/stream/stream.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolHormannBiSecur"

static const SubGhzBlockConst subghz_protocol_hormann_bisecur_const = {
    .te_short = 208,
    .te_long = 416,
    .te_delta = 104,
    .min_count_bit_for_found = 176,
};

struct SubGhzProtocolDecoderHormannBiSecur {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;

    uint8_t type;
    uint8_t data[22];
    uint8_t crc;
};

struct SubGhzProtocolEncoderHormannBiSecur {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t data[22];
};

typedef enum {
    HormannBiSecurDecoderStepReset = 0,
    HormannBiSecurDecoderStepFoundPreambleAlternatingShort,
    HormannBiSecurDecoderStepFoundPreambleHighVeryLong,
    HormannBiSecurDecoderStepFoundPreambleAlternatingLong,
    HormannBiSecurDecoderStepFoundData,
} HormannBiSecurDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hormann_bisecur_decoder = {
    .alloc = subghz_protocol_decoder_hormann_bisecur_alloc,
    .free = subghz_protocol_decoder_hormann_bisecur_free,

    .feed = subghz_protocol_decoder_hormann_bisecur_feed,
    .reset = subghz_protocol_decoder_hormann_bisecur_reset,

    .get_hash_data = subghz_protocol_decoder_hormann_bisecur_get_hash_data,
    .serialize = subghz_protocol_decoder_hormann_bisecur_serialize,
    .deserialize = subghz_protocol_decoder_hormann_bisecur_deserialize,
    .get_string = subghz_protocol_decoder_hormann_bisecur_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hormann_bisecur_encoder = {
    .alloc = subghz_protocol_encoder_hormann_bisecur_alloc,
    .free = subghz_protocol_encoder_hormann_bisecur_free,

    .deserialize = subghz_protocol_encoder_hormann_bisecur_deserialize,
    .stop = subghz_protocol_encoder_hormann_bisecur_stop,
    .yield = subghz_protocol_encoder_hormann_bisecur_yield,
};

const SubGhzProtocol subghz_protocol_hormann_bisecur = {
    .name = SUBGHZ_PROTOCOL_HORMANN_BISECUR_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_hormann_bisecur_decoder,
    .encoder = &subghz_protocol_hormann_bisecur_encoder,
};

/**
 * Calculates the next LevelDuration in an upload
 * @param result ManchesterEncoderResult
 * @return LevelDuration
 */
static LevelDuration
    subghz_protocol_encoder_hormann_bisecur_add_duration_to_upload(ManchesterEncoderResult result);

/**
 * Calculates CRC from the raw demodulated bytes
 * @param instance Pointer to a SubGhzProtocolDecoderHormannBiSecur instance
 */
static uint8_t subghz_protocol_decoder_hormann_bisecur_crc(SubGhzProtocolDecoderHormannBiSecur* instance);

/**
 * Checks if the raw demodulated data has correct CRC
 * @param instance Pointer to a SubGhzProtocolDecoderHormannBiSecur instance
 * @return if CRC is valid or not
 */
static bool subghz_protocol_decoder_hormann_bisecur_check_crc(SubGhzProtocolDecoderHormannBiSecur* instance);

/**
 * Add the next bit to the decoding result
 * @param instance Pointer to a SubGhzProtocolDecoderHormannBiSecur instance
 * @param level Level of the next bit
 */
static void subghz_protocol_decoder_hormann_bisecur_add_bit(SubGhzProtocolDecoderHormannBiSecur* instance, bool level);

/**
 * Parses the raw data into separate fields
 * @param instance Pointer to a SubGhzProtocolDecoderHormannBiSecur instance
 */
static void subghz_protocol_hormann_bisecur_parse_data(SubGhzProtocolDecoderHormannBiSecur* instance);

void* subghz_protocol_encoder_hormann_bisecur_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHormannBiSecur* instance = malloc(sizeof(SubGhzProtocolEncoderHormannBiSecur));

    instance->base.protocol = &subghz_protocol_hormann_bisecur;
    instance->generic.protocol_name = instance->base.protocol->name;

    // TODO insert 504.3ms carrier off delay between repeats with
    // instance->encoder.repeat = 3; //original remote does 3 repeats
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 21 * 2 + 2 * 2 +
        subghz_protocol_hormann_bisecur_const.min_count_bit_for_found * 2 + 1;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_hormann_bisecur_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHormannBiSecur* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderHormannBiSecur instance
 * @return true On success
 */
static bool subghz_protocol_encoder_hormann_bisecur_get_upload(SubGhzProtocolEncoderHormannBiSecur* instance) {
    furi_assert(instance);
    size_t index = 0;
    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    uint32_t duration_short = (uint32_t)subghz_protocol_hormann_bisecur_const.te_short;
    uint32_t duration_long = (uint32_t)subghz_protocol_hormann_bisecur_const.te_long;
    uint32_t duration_half_short = duration_short / 2;

    // Send preamble
    for (uint8_t i = 0; i < 21; i++) {
        uint32_t duration_low = duration_short;
        uint32_t duration_high = duration_short;

        if (i == 0) {
            duration_low += duration_half_short;
        }

        if (i == 20) {
            duration_high = duration_long * 4;
        }

        instance->encoder.upload[index++] = level_duration_make(false, duration_low);
        instance->encoder.upload[index++] = level_duration_make(true, duration_high);
    }

    for (uint8_t i = 0; i < 2; i++) {
        instance->encoder.upload[index++] = level_duration_make(false, duration_long);
        instance->encoder.upload[index++] = level_duration_make(true, duration_long);
    }

    // Send key data
    uint8_t max_byte_index = instance->generic.data_count_bit / 8 - 1;

    for (uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        uint8_t bit_index = i - 1;
        uint8_t byte_index = max_byte_index - bit_index / 8;
        bool bit_is_set = !bit_read(instance->data[byte_index], bit_index & 0x07);

        if (!manchester_encoder_advance(&enc_state, bit_is_set, &result)) {
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_hormann_bisecur_add_duration_to_upload(result);
            manchester_encoder_advance(&enc_state, bit_is_set, &result);
        }

        instance->encoder.upload[index++] =
            subghz_protocol_encoder_hormann_bisecur_add_duration_to_upload(result);
    }

    LevelDuration last_level_duration = subghz_protocol_encoder_hormann_bisecur_add_duration_to_upload(
        manchester_encoder_finish(&enc_state));

    // TODO: check with packet that ends in 0
    last_level_duration.duration += duration_short + duration_half_short;
    instance->encoder.upload[index++] = last_level_duration;

    instance->encoder.size_upload = index;

    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hormann_bisecur_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHormannBiSecur* instance = context;

    if (!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bits = 0;

    if (!flipper_format_read_uint32(flipper_format, "Bit", (uint32_t*)&bits, 1)) {
        FURI_LOG_E(TAG, "Missing Bit");
        return SubGhzProtocolStatusErrorParserBitCount;
    }

    instance->generic.data_count_bit = (uint16_t)bits;

    if (instance->generic.data_count_bit != subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        FURI_LOG_E(TAG, "Wrong number of bits in key");
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    size_t key_length = instance->generic.data_count_bit / 8;

    if (!flipper_format_read_hex(flipper_format, "Key", instance->data, key_length)) {
        FURI_LOG_E(TAG, "Unable to read Key in encoder");
        return SubGhzProtocolStatusErrorParserKey;
    }

    // optional parameter
    flipper_format_read_uint32(
        flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

    if (!subghz_protocol_encoder_hormann_bisecur_get_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;

    return SubGhzProtocolStatusOk;
}

void subghz_protocol_encoder_hormann_bisecur_stop(void* context) {
    SubGhzProtocolEncoderHormannBiSecur* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_hormann_bisecur_yield(void* context) {
    SubGhzProtocolEncoderHormannBiSecur* instance = context;

    if (instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if (++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void* subghz_protocol_decoder_hormann_bisecur_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHormannBiSecur* instance = malloc(sizeof(SubGhzProtocolDecoderHormannBiSecur));
    instance->base.protocol = &subghz_protocol_hormann_bisecur;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hormann_bisecur_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    free(instance);
}

void subghz_protocol_decoder_hormann_bisecur_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
    memset(instance->data, 0, 22);
    instance->generic.data_count_bit = 0;
    instance->manchester_saved_state = 0;
}

void subghz_protocol_decoder_hormann_bisecur_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    uint32_t duration_short = (uint32_t)subghz_protocol_hormann_bisecur_const.te_short;
    uint32_t duration_long = (uint32_t)subghz_protocol_hormann_bisecur_const.te_long;
    uint32_t duration_delta = (uint32_t)subghz_protocol_hormann_bisecur_const.te_delta;
    uint32_t duration_half_short = duration_short / 2;

    ManchesterEvent event = ManchesterEventReset;

    switch (instance->decoder.parser_step) {
        case HormannBiSecurDecoderStepReset:
            if (!level && DURATION_DIFF(duration, duration_short + duration_half_short) <
                    duration_delta) {
                instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleAlternatingShort;
            }
            break;
        case HormannBiSecurDecoderStepFoundPreambleAlternatingShort:
            if (level && DURATION_DIFF(duration, duration_long * 4) < duration_delta) {
                instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleHighVeryLong;
                break;
            }

            if (DURATION_DIFF(duration, duration_short) < duration_delta) {
                // stay on the same step
                break;
            }

            instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
            break;
        case HormannBiSecurDecoderStepFoundPreambleHighVeryLong:
            if (!level && DURATION_DIFF(duration, duration_long) < duration_delta) {
                instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleAlternatingLong;
                break;
            }

            instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
            break;
        case HormannBiSecurDecoderStepFoundPreambleAlternatingLong:
            // so far, the first bit is always 0, e.g. 0b01010000, 0b01110000
            if (!level && DURATION_DIFF(duration, duration_short) < duration_delta) {
                manchester_advance(instance->manchester_saved_state, ManchesterEventShortLow,
                    &instance->manchester_saved_state, NULL);
                instance->decoder.parser_step = HormannBiSecurDecoderStepFoundData;
                break;
            }

            if (DURATION_DIFF(duration, duration_long) < duration_delta) {
                // stay on the same step
                break;
            }

            instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
            break;
        case HormannBiSecurDecoderStepFoundData:
            if (DURATION_DIFF(duration, duration_short) < duration_delta || (
                instance->generic.data_count_bit == subghz_protocol_hormann_bisecur_const.min_count_bit_for_found - 1 &&
                DURATION_DIFF(duration, duration_long + duration_short + duration_half_short) < duration_delta
            )) {
                event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
            }
            else {
                if (DURATION_DIFF(duration, duration_long) < duration_delta) {
                    event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
                }
            }

            if (event == ManchesterEventReset) {
                subghz_protocol_decoder_hormann_bisecur_reset(instance);
            }
            else {
                bool new_level;

                if (manchester_advance(instance->manchester_saved_state, event,
                        &instance->manchester_saved_state, &new_level)) {
                    subghz_protocol_decoder_hormann_bisecur_add_bit(instance, new_level);
                }
            }
            break;
    }
}

uint8_t subghz_protocol_decoder_hormann_bisecur_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    uint8_t hash = 0;
    size_t key_length = instance->generic.data_count_bit / 8;

    for (size_t i = 0; i < key_length; i++) {
        hash ^= instance->data[i];
    }

    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_hormann_bisecur_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    FuriString* preset_name = furi_string_alloc();

    do {
        stream_clean(flipper_format_get_raw_stream(flipper_format));

        if (!flipper_format_write_header_cstr(
               flipper_format, SUBGHZ_KEY_FILE_TYPE, SUBGHZ_KEY_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Unable to add header");
            res = SubGhzProtocolStatusErrorParserHeader;
            break;
        }

        if (!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) {
            FURI_LOG_E(TAG, "Unable to add Frequency");
            res = SubGhzProtocolStatusErrorParserFrequency;
            break;
        }

        subghz_block_generic_get_preset_name(furi_string_get_cstr(preset->name), preset_name);

        if (!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(preset_name))) {
            FURI_LOG_E(TAG, "Unable to add Preset");
            res = SubGhzProtocolStatusErrorParserPreset;
            break;
        }

        if (!strcmp(furi_string_get_cstr(preset_name), "FuriHalSubGhzPresetCustom")) {
            if (!flipper_format_write_string_cstr(
                   flipper_format, "Custom_preset_module", "CC1101")) {
                FURI_LOG_E(TAG, "Unable to add Custom_preset_module");
                res = SubGhzProtocolStatusErrorParserCustomPreset;
                break;
            }

            if (!flipper_format_write_hex(
                   flipper_format, "Custom_preset_data", preset->data, preset->data_size)) {
                FURI_LOG_E(TAG, "Unable to add Custom_preset_data");
                res = SubGhzProtocolStatusErrorParserCustomPreset;
                break;
            }
        }

        if (!flipper_format_write_string_cstr(flipper_format, "Protocol",
                instance->generic.protocol_name)) {
            FURI_LOG_E(TAG, "Unable to add Protocol");
            res = SubGhzProtocolStatusErrorParserProtocolName;
            break;
        }

        uint32_t bit = instance->generic.data_count_bit;

        if (!flipper_format_write_uint32(flipper_format, "Bit", &bit, 1)) {
            FURI_LOG_E(TAG, "Unable to add Bit");
            res = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }

        uint16_t key_length = instance->generic.data_count_bit / 8;

        if (!flipper_format_write_hex(flipper_format, "Key", instance->data, key_length)) {
            FURI_LOG_E(TAG, "Unable to add Key");
            res = SubGhzProtocolStatusErrorParserKey;
            break;
        }
    } while (false);

    furi_string_free(preset_name);

    return res;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hormann_bisecur_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    if (!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bits = 0;

    if (!flipper_format_read_uint32(flipper_format, "Bit", (uint32_t*)&bits, 1)) {
        FURI_LOG_E(TAG, "Missing Bit");
        return SubGhzProtocolStatusErrorParserBitCount;
    }

    instance->generic.data_count_bit = (uint16_t)bits;

    if (instance->generic.data_count_bit != subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        FURI_LOG_E(TAG, "Wrong number of bits in key");
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    size_t key_length = instance->generic.data_count_bit / 8;

    if (!flipper_format_read_hex(flipper_format, "Key", instance->data, key_length)) {
        FURI_LOG_E(TAG, "Unable to read Key in decoder");
        return SubGhzProtocolStatusErrorParserKey;
    }

    subghz_protocol_hormann_bisecur_parse_data(instance);

    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_hormann_bisecur_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    bool valid_crc = subghz_protocol_decoder_hormann_bisecur_check_crc(instance);

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "%dbit CRC:0x%02X %s\r\n"
        "Type:0x%02X Sn:0x%08lX\r\n"
        "Key:%016llX\r\n"
        "Key:%016llX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit, instance->crc, valid_crc ? "OK" : "WRONG",
        instance->type, instance->generic.serial,
        instance->generic.data,
        instance->generic.data_2
    );
}

static LevelDuration
    subghz_protocol_encoder_hormann_bisecur_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {
        .duration = 0,
        .level = 0
    };

    switch (result) {
        case ManchesterEncoderResultShortLow:
            data.duration = subghz_protocol_hormann_bisecur_const.te_short;
            data.level = false;
            break;
        case ManchesterEncoderResultLongLow:
            data.duration = subghz_protocol_hormann_bisecur_const.te_long;
            data.level = false;
            break;
        case ManchesterEncoderResultLongHigh:
            data.duration = subghz_protocol_hormann_bisecur_const.te_long;
            data.level = true;
            break;
        case ManchesterEncoderResultShortHigh:
            data.duration = subghz_protocol_hormann_bisecur_const.te_short;
            data.level = true;
            break;

        default:
            furi_crash("SubGhz: ManchesterEncoderResult is incorrect.");
            break;
    }

    return level_duration_make(data.level, data.duration);
}

static uint8_t subghz_protocol_decoder_hormann_bisecur_crc(SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    switch (instance->type) {
        case 0x50:
            return ~(subghz_protocol_blocks_crc8(instance->data + 1, 20, 0x07, 0x00) ^ 0x55);
        case 0x70:
            return subghz_protocol_blocks_crc8le(instance->data, 21, 0x07, 0xFF);
    }

    FURI_LOG_E(TAG, "Unknown type 0x%02X", instance->type);

    return 0;
}

static bool subghz_protocol_decoder_hormann_bisecur_check_crc(SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    if (instance->type != 0x50 && instance->type != 0x70) {
        FURI_LOG_W(TAG, "Unknown type 0x%02X", instance->type);
        return false;
    }

    return subghz_protocol_decoder_hormann_bisecur_crc(instance) == instance->crc;
}

static void subghz_protocol_hormann_bisecur_parse_data(SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    instance->type = instance->data[0];

    instance->generic.serial = 0;

    for (uint8_t i = 1; i < 5; i++) {
        instance->generic.serial = instance->generic.serial << 8 | instance->data[i];
    }

    instance->generic.data = 0;

    for (uint8_t i = 5; i < 13; i++) {
        instance->generic.data = instance->generic.data << 8 | instance->data[i];
    }

    instance->generic.data_2 = 0;

    for (uint8_t i = 13; i < 21; i++) {
        instance->generic.data_2 = instance->generic.data_2 << 8 | instance->data[i];
    }

    instance->crc = instance->data[21];
}

static void subghz_protocol_decoder_hormann_bisecur_add_bit(SubGhzProtocolDecoderHormannBiSecur* instance, bool level) {
    furi_assert(instance);

    if (instance->generic.data_count_bit >= subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        return;
    }

    if (level) {
        uint8_t byte_index = instance->generic.data_count_bit / 8;
        uint8_t bit_index = instance->generic.data_count_bit % 8;

        instance->data[byte_index] |= 1 << (7 - bit_index);
    }

    instance->generic.data_count_bit++;

    if (instance->generic.data_count_bit >= subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        if (instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        else {
            subghz_protocol_decoder_hormann_bisecur_reset(instance);
        }
    }
}
