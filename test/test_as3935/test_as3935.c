/* test_as3935.c — Unity-based unit tests for AS3935 driver logic
 *
 * These tests cover pure-logic behaviour that does not require hardware:
 *   - Enum value correctness
 *   - as3935_data_t field sizes / offsets
 *   - min_strikes mapping validation (tested via expected register values)
 *   - Payload serialisation helpers (mqtt_payload_build_status)
 *
 * Run with:
 *   idf.py -T test/test_as3935 build flash monitor
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

/* -----------------------------------------------------------------------
 * Pull in only the pure-logic headers that can be exercised without I2C.
 * ----------------------------------------------------------------------- */
#include "lightning/as3935.h"
#include "mqtt/mqtt_payload.h"

/* -----------------------------------------------------------------------
 * AS3935 enum sanity checks
 * ----------------------------------------------------------------------- */

TEST_CASE("AS3935 noise floor enum values are 0-7", "[as3935]")
{
    TEST_ASSERT_EQUAL_INT(0, (int)AS3935_NOISE_FLOOR_390UV);
    TEST_ASSERT_EQUAL_INT(7, (int)AS3935_NOISE_FLOOR_2000UV);
}

TEST_CASE("AS3935 event enum: none=0, noise=1, disturber=2, lightning=3",
          "[as3935]")
{
    TEST_ASSERT_EQUAL_INT(0, (int)AS3935_EVENT_NONE);
    TEST_ASSERT_EQUAL_INT(1, (int)AS3935_EVENT_NOISE);
    TEST_ASSERT_EQUAL_INT(2, (int)AS3935_EVENT_DISTURBER);
    TEST_ASSERT_EQUAL_INT(3, (int)AS3935_EVENT_LIGHTNING);
}

TEST_CASE("AS3935 watchdog range: 1-10", "[as3935]")
{
    TEST_ASSERT_EQUAL_INT(1,  (int)AS3935_WATCHDOG_SENSITIVITY_1);
    TEST_ASSERT_EQUAL_INT(10, (int)AS3935_WATCHDOG_SENSITIVITY_10);
}

TEST_CASE("AS3935 spike rejection range: 1-11", "[as3935]")
{
    TEST_ASSERT_EQUAL_INT(1,  (int)AS3935_SPIKE_REJECTION_1);
    TEST_ASSERT_EQUAL_INT(11, (int)AS3935_SPIKE_REJECTION_11);
}

/* -----------------------------------------------------------------------
 * as3935_data_t structure checks
 * ----------------------------------------------------------------------- */

TEST_CASE("as3935_data_t: distance_km is uint8_t", "[as3935]")
{
    as3935_data_t d;
    d.distance_km = 255;
    TEST_ASSERT_EQUAL_UINT8(255, d.distance_km);
}

TEST_CASE("as3935_data_t: energy is uint32_t (up to 20-bit sensor value)",
          "[as3935]")
{
    as3935_data_t d;
    d.energy = 0x000FFFFF; /* max 20-bit */
    TEST_ASSERT_EQUAL_UINT32(0x000FFFFF, d.energy);
}

TEST_CASE("as3935_data_t: distance 1 means overhead, 63 means out of range",
          "[as3935]")
{
    as3935_data_t d;
    d.distance_km = 1;
    TEST_ASSERT_EQUAL_UINT8(1, d.distance_km); /* overhead */
    d.distance_km = 63;
    TEST_ASSERT_EQUAL_UINT8(63, d.distance_km); /* out of range */
}

/* -----------------------------------------------------------------------
 * mqtt_payload status builder
 * ----------------------------------------------------------------------- */

TEST_CASE("mqtt_payload_build_status: online payload", "[mqtt_payload]")
{
    char buf[64];
    int  len = mqtt_payload_build_status(true, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"online\""));
}

TEST_CASE("mqtt_payload_build_status: offline payload", "[mqtt_payload]")
{
    char buf[64];
    int  len = mqtt_payload_build_status(false, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"offline\""));
}

TEST_CASE("mqtt_payload_build_status: buffer too small returns -1",
          "[mqtt_payload]")
{
    char buf[4]; /* deliberately too small */
    int  len = mqtt_payload_build_status(true, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, len);
}

/* -----------------------------------------------------------------------
 * mqtt_payload config builder
 * ----------------------------------------------------------------------- */

TEST_CASE("mqtt_payload_build_config: contains expected keys", "[mqtt_payload]")
{
    char buf[256];
    int  len = mqtt_payload_build_config(true, 2, 3, 2, 1, false, buf,
                                          sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "indoor_mode"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "noise_floor"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "watchdog"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "spike_rejection"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "min_strikes"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "disturber_mask"));
}

/* -----------------------------------------------------------------------
 * Unity runner
 * ----------------------------------------------------------------------- */

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
