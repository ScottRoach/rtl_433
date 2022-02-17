/** @file
    EMOS E6016 weatherstation with DCF77.

    Copyright (C) 2022 Dirk Utke-Woehlke <kardinal26@mail.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
EMOS E6016 weatherstation with DCF77.

DCF77 not supported currently.

- Manufacturer: EMOS
- Transmit Interval: every ~61 s
- Frequency: 433.92 MHz
- Modulation: OOK PWM, INVERTED

Data Layout:

    PP PP PP II BK KK KK KK CT TT HH SS D? XX RR

- P: (24 bit) preamble
- I: (8 bit) ID
- B: (4 bit) battery indication
- K: (30 bit) datetime, encoding not known
- C: (2 bit) channel
- T: (12 bit) temperature, signed, scale 10
- H: (8 bit) humidity
- S: (8 bit) wind speed
- D: (4 bit) wind direction
- ?: (4 bit) unknown
- X: (8 bit) checksum
- R: (8 bit) repeat counter

Raw data:

    [00] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 ff
    [01] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fe
    [02] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fd
    [03] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fc
    [04] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fb
    [05] {120} 55 5a 7c 00 6a a5 60 e7 3f 36 da ff 5d 38 fa

Format string:

    MODEL?:8h8h8h ID?:8d BAT?4d DAY?13d T?5d:6d:6d CH:2d TEMP:12d HUM?8d WSPEED:8d WINDIR:4d ?4h CHK:8h REPEAT:8h

Decoded example:

    MODEL?:aaa583 ID?:255 BAT?09 DAY:2741 T07:49:35 CH:0 TEMP:0201 HUM?037 WSPEED:000 WINDIR:10 ?2 CHK:c7 REPEAT:00

*/

static int emos_e6016_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 120 - 8); // ignores the repeat byte

    if (r < 0) {
        decoder_log(decoder, 2, __func__, "Repeated row fail");
        return DECODE_ABORT_EARLY;
    }
    decoder_logf(decoder, 2, __func__, "Found row: %d", r);

    uint8_t *b = bitbuffer->bb[r];
    // we expect 120 bits
    if (bitbuffer->bits_per_row[r] != 120) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }

    // model check 55 5a 7c
    if (b[0] != 0x55 || b[1] != 0x5a || b[2] != 0x7c) {
        decoder_log(decoder, 2, __func__, "Model check fail");
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    // check checksum
    if ((add_bytes(b, 13) & 0xff) != b[13]) {
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int id         = b[3];
    int battery    = ((b[4] & 0xf0) >> 4);
    int dcf77_raw  = ((b[4] & 0x0f) << 26) | (b[5] << 18) | (b[6] << 10) | (b[7] << 2) | (b[8] >> 6);
    int dcf77_sec  = ((dcf77_raw >> 0) & 0x3f);
    int dcf77_min  = ((dcf77_raw >> 6) & 0x3f);
    int dcf77_hour = ((dcf77_raw >> 12) & 0x1f);
    int dcf77_days = (dcf77_raw >> 17); // unknown coding
    int channel    = ((b[8] >> 4) & 0x3) + 1;
    int temp_raw   = (int16_t)(((b[8] & 0x0f) << 12) | (b[9] << 4)); // use sign extend
    float temp_c   = (temp_raw >> 4) * 0.1f;
    int humidity   = b[10];
    float speed_ms = b[11];
    int dir_raw    = (((b[12] & 0xf0) >> 4));
    float dir_deg  = dir_raw * 22.5f;

    char dcf77_str[14]; // "8192T32:64:64"
    sprintf(dcf77_str, "%dT%d:%d:%d", dcf77_days, dcf77_hour, dcf77_min, dcf77_sec);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "EMOS-E6016",
            "id",               "House Code",       DATA_INT,    id,
            "channel",          "Channel",          DATA_INT,    channel,
            "battery_ok",       "Battery_OK",       DATA_INT,    !!battery,
            "temperature_C",    "Temperature_C",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u", DATA_INT, humidity,
            "wind_avg_m_s",     "WindSpeed m_s",    DATA_FORMAT, "%.1f",  DATA_DOUBLE, speed_ms,
            "wind_dir_deg",     "Wind direction",   DATA_FORMAT, "%.1f",  DATA_DOUBLE, dir_deg,
            "datetime_raw",     "Raw DCF77",        DATA_FORMAT, "%08x",  DATA_INT, dcf77_raw,
            "datetime_maybe",   "Maybe DCF77",      DATA_STRING, dcf77_str,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_m_s",
        "wind_dir_deg",
        "datetime_raw",
        "datetime_maybe",
        "mic",
        NULL,
};
// n=EMOS-E6016,m=OOK_PWM,s=280,l=796,r=804,g=0,t=0,y=1836,rows>=3,bits=120
r_device emos_e6016 = {
        .name        = "EMOS E6016 weatherstation with DCF77",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 280,
        .long_width  = 796,
        .gap_limit   = 3000,
        .reset_limit = 804,
        .sync_width  = 1836,
        .decode_fn   = &emos_e6016_decode,
        .fields      = output_fields,
};
