#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "amulet_ble_classifier.h"

static int failure_count;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            failure_count++; \
        } \
    } while (0)

static bool classification_equals(
    amulet_ble_classification_t classification,
    amulet_ble_rule_t rule,
    amulet_ble_category_t category,
    amulet_ble_confidence_t confidence
)
{
    return classification.rule == rule &&
           classification.category == category &&
           classification.confidence == confidence;
}

static bool classification_is_none(amulet_ble_classification_t classification)
{
    return classification_equals(
        classification,
        AMULET_BLE_RULE_NONE,
        AMULET_BLE_CATEGORY_NONE,
        AMULET_BLE_CONFIDENCE_NONE
    );
}

static bool classify_advertisement_without_metadata(
    const uint8_t *data,
    size_t length,
    amulet_ble_advertisement_t *advertisement
)
{
    return amulet_ble_classify_advertisement(
        data,
        length,
        NULL,
        advertisement
    );
}

static void test_exact_tracker_fixture(void)
{
    static const uint8_t fixture[] = {
        0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
    };
    amulet_ble_classification_t classification =
        amulet_ble_classify_manufacturer_data(fixture, sizeof(fixture));

    CHECK(classification_equals(
        classification,
        AMULET_BLE_RULE_TEST_TRACKER,
        AMULET_BLE_CATEGORY_WORKSHOP_FIXTURE,
        AMULET_BLE_CONFIDENCE_EXACT
    ));
}

static void test_tracker_near_matches(void)
{
    static const uint8_t fixture[] = {
        0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
    };
    static const uint8_t same_company_other_payload[] = {
        0xff, 0xff, 'N', 'O', 'P', 'E', 0x01,
    };

    CHECK(classification_is_none(
        amulet_ble_classify_manufacturer_data(NULL, sizeof(fixture))
    ));
    CHECK(classification_is_none(
        amulet_ble_classify_manufacturer_data(
            same_company_other_payload,
            sizeof(same_company_other_payload)
        )
    ));
    for (size_t length = 0; length < sizeof(fixture); length++) {
        CHECK(classification_is_none(
            amulet_ble_classify_manufacturer_data(fixture, length)
        ));
    }

    uint8_t extended[sizeof(fixture) + 1];
    memcpy(extended, fixture, sizeof(fixture));
    extended[sizeof(fixture)] = 0;
    CHECK(classification_is_none(
        amulet_ble_classify_manufacturer_data(extended, sizeof(extended))
    ));

    for (size_t index = 0; index < sizeof(fixture); index++) {
        uint8_t mutated[sizeof(fixture)];
        memcpy(mutated, fixture, sizeof(fixture));
        mutated[index] ^= 0x01;
        CHECK(classification_is_none(
            amulet_ble_classify_manufacturer_data(mutated, sizeof(mutated))
        ));
    }
}

static void test_exact_peer_uuid(void)
{
    static const uint8_t fixture[] = {
        0x34, 0x36, 0x11, 0xaf, 0x30, 0x32, 0x05, 0xa9,
        0x44, 0x43, 0xae, 0xc8, 0x50, 0xba, 0xdb, 0x33,
    };
    amulet_ble_classification_t classification =
        amulet_ble_classify_service_uuid128(fixture, sizeof(fixture));

    CHECK(classification_equals(
        classification,
        AMULET_BLE_RULE_PEER_AMULET,
        AMULET_BLE_CATEGORY_LOCAL_PROTOCOL,
        AMULET_BLE_CONFIDENCE_EXACT
    ));
}

static void test_peer_uuid_near_matches(void)
{
    static const uint8_t fixture[] = {
        0x34, 0x36, 0x11, 0xaf, 0x30, 0x32, 0x05, 0xa9,
        0x44, 0x43, 0xae, 0xc8, 0x50, 0xba, 0xdb, 0x33,
    };
    static const uint8_t canonical_text_order[] = {
        0x33, 0xdb, 0xba, 0x50, 0xc8, 0xae, 0x43, 0x44,
        0xa9, 0x05, 0x32, 0x30, 0xaf, 0x11, 0x36, 0x34,
    };

    CHECK(classification_is_none(
        amulet_ble_classify_service_uuid128(NULL, sizeof(fixture))
    ));
    CHECK(classification_is_none(
        amulet_ble_classify_service_uuid128(
            canonical_text_order,
            sizeof(canonical_text_order)
        )
    ));
    for (size_t length = 0; length < sizeof(fixture); length++) {
        CHECK(classification_is_none(
            amulet_ble_classify_service_uuid128(fixture, length)
        ));
    }

    uint8_t extended[sizeof(fixture) + 1];
    memcpy(extended, fixture, sizeof(fixture));
    extended[sizeof(fixture)] = 0;
    CHECK(classification_is_none(
        amulet_ble_classify_service_uuid128(extended, sizeof(extended))
    ));

    for (size_t index = 0; index < sizeof(fixture); index++) {
        uint8_t mutated[sizeof(fixture)];
        memcpy(mutated, fixture, sizeof(fixture));
        mutated[index] ^= 0x01;
        CHECK(classification_is_none(
            amulet_ble_classify_service_uuid128(mutated, sizeof(mutated))
        ));
    }
}

static void test_ibeacon_fixture(void)
{
    static const uint8_t fixture[] = {
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd,
        0xc5,
    };
    static const uint8_t expected_identity[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd,
    };
    amulet_ble_classification_t classification =
        amulet_ble_classify_manufacturer_data(fixture, sizeof(fixture));
    amulet_ble_ibeacon_t ibeacon;

    CHECK(classification_equals(
        classification,
        AMULET_BLE_RULE_IBEACON,
        AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        AMULET_BLE_CONFIDENCE_STRUCTURAL
    ));
    CHECK(amulet_ble_parse_ibeacon(fixture, sizeof(fixture), &ibeacon));
    CHECK(memcmp(
        ibeacon.identity,
        expected_identity,
        sizeof(expected_identity)
    ) == 0);
    CHECK(ibeacon.calibration_power == -59);
}

static void test_ibeacon_near_matches(void)
{
    static const uint8_t fixture[] = {
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd,
        0xc5,
    };

    CHECK(!amulet_ble_parse_ibeacon(NULL, sizeof(fixture), NULL));
    for (size_t length = 0; length < sizeof(fixture); length++) {
        CHECK(!amulet_ble_parse_ibeacon(fixture, length, NULL));
    }

    uint8_t extended[sizeof(fixture) + 1];
    memcpy(extended, fixture, sizeof(fixture));
    extended[sizeof(fixture)] = 0;
    CHECK(!amulet_ble_parse_ibeacon(extended, sizeof(extended), NULL));

    for (size_t index = 0; index < 4; index++) {
        uint8_t mutated[sizeof(fixture)];
        memcpy(mutated, fixture, sizeof(fixture));
        mutated[index] ^= 0x01;
        CHECK(!amulet_ble_parse_ibeacon(mutated, sizeof(mutated), NULL));
        CHECK(classification_is_none(
            amulet_ble_classify_manufacturer_data(mutated, sizeof(mutated))
        ));
    }

    for (size_t index = 4; index < sizeof(fixture); index++) {
        uint8_t mutated[sizeof(fixture)];
        memcpy(mutated, fixture, sizeof(fixture));
        mutated[index] ^= 0x01;
        CHECK(amulet_ble_parse_ibeacon(mutated, sizeof(mutated), NULL));
    }

    amulet_ble_ibeacon_t output;
    memset(&output, 0xa5, sizeof(output));
    CHECK(!amulet_ble_parse_ibeacon(fixture, sizeof(fixture) - 1, &output));
    const uint8_t zero[sizeof(output)] = {0};
    CHECK(memcmp(&output, zero, sizeof(output)) == 0);
}

static void test_ibeacon_calibration_boundaries(void)
{
    uint8_t fixture[] = {
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd,
        0x80,
    };
    amulet_ble_ibeacon_t ibeacon;

    CHECK(amulet_ble_parse_ibeacon(fixture, sizeof(fixture), &ibeacon));
    CHECK(ibeacon.calibration_power == INT8_MIN);
    fixture[sizeof(fixture) - 1] = 0x7f;
    CHECK(amulet_ble_parse_ibeacon(fixture, sizeof(fixture), &ibeacon));
    CHECK(ibeacon.calibration_power == INT8_MAX);
}

static void test_altbeacon_fixture_and_evidence(void)
{
    static const uint8_t fixture[] = {
        0x18, 0x01, 0xbe, 0xac,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13,
        0xc5, 0x7f,
    };
    static const uint8_t expected_beacon_id[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13,
    };
    amulet_ble_classification_t classification =
        amulet_ble_classify_manufacturer_data(fixture, sizeof(fixture));
    amulet_ble_altbeacon_t altbeacon;

    CHECK(classification_equals(
        classification,
        AMULET_BLE_RULE_ALTBEACON,
        AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        AMULET_BLE_CONFIDENCE_STRUCTURAL
    ));
    CHECK(classification.ambient_evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH);
    CHECK(amulet_ble_parse_altbeacon(
        fixture,
        sizeof(fixture),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        &altbeacon
    ));
    CHECK(altbeacon.company_id == 0x0118);
    CHECK(memcmp(
        altbeacon.beacon_id,
        expected_beacon_id,
        sizeof(expected_beacon_id)
    ) == 0);
    CHECK(altbeacon.reference_rssi == -59);
    CHECK(altbeacon.reserved == 0x7f);
    CHECK(altbeacon.evidence == AMULET_BLE_ALTBEACON_STRICT_V1);
    CHECK(amulet_ble_altbeacon_evidence_is_dispatchable(altbeacon.evidence));

    CHECK(amulet_ble_parse_altbeacon(
        fixture,
        sizeof(fixture),
        AMULET_BLE_PDU_TYPE_UNAVAILABLE,
        &altbeacon
    ));
    CHECK(altbeacon.evidence == AMULET_BLE_ALTBEACON_PAYLOAD_MATCH);
    CHECK(amulet_ble_altbeacon_evidence_is_dispatchable(altbeacon.evidence));

    CHECK(amulet_ble_parse_altbeacon(
        fixture,
        sizeof(fixture),
        AMULET_BLE_PDU_TYPE_OTHER,
        &altbeacon
    ));
    CHECK(altbeacon.evidence == AMULET_BLE_ALTBEACON_ENVELOPE_CONFLICT);
    CHECK(!amulet_ble_altbeacon_evidence_is_dispatchable(altbeacon.evidence));

    uint8_t unassigned[sizeof(fixture)];
    memcpy(unassigned, fixture, sizeof(unassigned));
    unassigned[0] = 0x19;
    unassigned[1] = 0x01;
    CHECK(amulet_ble_parse_altbeacon(
        unassigned,
        sizeof(unassigned),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        &altbeacon
    ));
    CHECK(altbeacon.company_id == 0x0119);
    CHECK(altbeacon.evidence == AMULET_BLE_ALTBEACON_SHAPED);
    CHECK(!amulet_ble_altbeacon_evidence_is_dispatchable(altbeacon.evidence));
    classification = amulet_ble_classify_manufacturer_data(
        unassigned,
        sizeof(unassigned)
    );
    CHECK(classification.ambient_evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_SHAPED);

    unassigned[0] = 0x17;
    unassigned[1] = 0x11;
    CHECK(amulet_ble_parse_altbeacon(
        unassigned,
        sizeof(unassigned),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        &altbeacon
    ));
    CHECK(altbeacon.company_id == 0x1117);
    CHECK(altbeacon.evidence == AMULET_BLE_ALTBEACON_SHAPED);
}

static void test_altbeacon_near_matches(void)
{
    static const uint8_t fixture[] = {
        0x18, 0x01, 0xbe, 0xac,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13,
        0xc5, 0x00,
    };

    CHECK(!amulet_ble_parse_altbeacon(
        NULL,
        sizeof(fixture),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));
    for (size_t length = 0; length < sizeof(fixture); length++) {
        CHECK(!amulet_ble_parse_altbeacon(
            fixture,
            length,
            AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
            NULL
        ));
    }

    uint8_t extended[sizeof(fixture) + 1];
    memcpy(extended, fixture, sizeof(fixture));
    extended[sizeof(fixture)] = 0;
    CHECK(!amulet_ble_parse_altbeacon(
        extended,
        sizeof(extended),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));

    for (size_t index = 2; index < 4; index++) {
        uint8_t mutated[sizeof(fixture)];
        memcpy(mutated, fixture, sizeof(mutated));
        mutated[index] ^= 0x01;
        CHECK(!amulet_ble_parse_altbeacon(
            mutated,
            sizeof(mutated),
            AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
            NULL
        ));
        CHECK(classification_is_none(
            amulet_ble_classify_manufacturer_data(mutated, sizeof(mutated))
        ));
    }

    uint8_t rssi_boundary[sizeof(fixture)];
    memcpy(rssi_boundary, fixture, sizeof(rssi_boundary));
    rssi_boundary[24] = 0x80;
    CHECK(!amulet_ble_parse_altbeacon(
        rssi_boundary,
        sizeof(rssi_boundary),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));
    rssi_boundary[24] = 0x01;
    CHECK(!amulet_ble_parse_altbeacon(
        rssi_boundary,
        sizeof(rssi_boundary),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));
    rssi_boundary[24] = 0x81;
    CHECK(amulet_ble_parse_altbeacon(
        rssi_boundary,
        sizeof(rssi_boundary),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));
    rssi_boundary[24] = 0x00;
    CHECK(amulet_ble_parse_altbeacon(
        rssi_boundary,
        sizeof(rssi_boundary),
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        NULL
    ));

    amulet_ble_altbeacon_t output;
    memset(&output, 0xa5, sizeof(output));
    CHECK(!amulet_ble_parse_altbeacon(
        fixture,
        sizeof(fixture) - 1,
        AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
        &output
    ));
    const uint8_t zero[sizeof(output)] = {0};
    CHECK(memcmp(&output, zero, sizeof(output)) == 0);
}

static void test_altbeacon_advertisement_dispatch(void)
{
    static const uint8_t field[] = {
        0x1b, 0xff,
        0x18, 0x01, 0xbe, 0xac,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13,
        0xc5, 0x00,
    };
    static const amulet_ble_scan_metadata_t nonconnectable = {
        .pdu_type = AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
    };
    static const amulet_ble_scan_metadata_t conflicting = {
        .pdu_type = AMULET_BLE_PDU_TYPE_OTHER,
    };
    amulet_ble_advertisement_t advertisement;

    CHECK(amulet_ble_classify_advertisement(
        field,
        sizeof(field),
        &nonconnectable,
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    const amulet_ble_ambient_marker_t *marker = &advertisement.ambient_markers[0];
    CHECK(marker->rule == AMULET_BLE_RULE_ALTBEACON);
    CHECK(marker->observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER);
    CHECK(marker->evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1);
    CHECK(marker->multiplicity == AMULET_BLE_CANDIDATE_MULTIPLICITY_ONE);
    CHECK(marker->identity_length == AMULET_ALTBEACON_IDENTITY_LENGTH);
    CHECK(marker->identity[0] == 0x18);
    CHECK(marker->identity[1] == 0x01);
    CHECK(marker->identity[2] == 0x00);
    CHECK(marker->identity[21] == 0x13);
    CHECK(marker->calibration_power == -59);
    CHECK(marker->calibration_power_valid);
    CHECK(amulet_ble_primary_action(&advertisement) ==
          AMULET_BLE_ACTION_AMBIENT);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(classify_advertisement_without_metadata(
        field,
        sizeof(field),
        &advertisement
    ));
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(amulet_ble_classify_advertisement(
        field,
        sizeof(field),
        &conflicting,
        &advertisement
    ));
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    CHECK(advertisement.ambient_markers[0].identity_length == 0);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t unassigned[sizeof(field)];
    memcpy(unassigned, field, sizeof(unassigned));
    unassigned[2] = 0x19;
    unassigned[3] = 0x01;
    CHECK(amulet_ble_classify_advertisement(
        unassigned,
        sizeof(unassigned),
        &nonconnectable,
        &advertisement
    ));
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_SHAPED);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t repeated_pair[sizeof(field) * 2];
    memcpy(repeated_pair, field, sizeof(field));
    memcpy(repeated_pair + sizeof(field), field, sizeof(field));
    CHECK(classify_advertisement_without_metadata(
        repeated_pair,
        sizeof(repeated_pair),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t repeated[sizeof(field) * (AMULET_BLE_MAX_AMBIENT_MARKERS + 1U)];
    for (size_t index = 0; index < AMULET_BLE_MAX_AMBIENT_MARKERS + 1U; index++) {
        memcpy(repeated + index * sizeof(field), field, sizeof(field));
    }
    CHECK(classify_advertisement_without_metadata(
        repeated,
        sizeof(repeated),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_overflow);
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    CHECK(advertisement.ambient_markers[0].multiplicity ==
          AMULET_BLE_CANDIDATE_MULTIPLICITY_OVERFLOW);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t wrong_type[sizeof(field)];
    memcpy(wrong_type, field, sizeof(wrong_type));
    wrong_type[1] = 0x16;
    CHECK(classify_advertisement_without_metadata(
        wrong_type,
        sizeof(wrong_type),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 0);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t impossible_legacy[sizeof(field) + 4];
    memcpy(impossible_legacy, field, sizeof(field));
    impossible_legacy[sizeof(field)] = 0x03;
    impossible_legacy[sizeof(field) + 1] = 0x09;
    impossible_legacy[sizeof(field) + 2] = 'a';
    impossible_legacy[sizeof(field) + 3] = 'b';
    CHECK(amulet_ble_classify_advertisement(
        impossible_legacy,
        sizeof(impossible_legacy),
        &nonconnectable,
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(!amulet_ble_classify_advertisement(
        field,
        sizeof(field) - 1,
        &nonconnectable,
        &advertisement
    ));

    uint8_t overlong[sizeof(field) + 1];
    overlong[0] = 0x1c;
    overlong[1] = 0xff;
    memcpy(overlong + 2, field + 2, sizeof(field) - 2);
    overlong[sizeof(overlong) - 1] = 0;
    CHECK(amulet_ble_classify_advertisement(
        overlong,
        sizeof(overlong),
        &nonconnectable,
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 0);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t split[sizeof(field) + 2] = {
        0x0e, 0xff,
    };
    memcpy(split + 2, field + 2, 13);
    split[15] = 0x0e;
    split[16] = 0xff;
    memcpy(split + 17, field + 15, 13);
    CHECK(amulet_ble_classify_advertisement(
        split,
        sizeof(split),
        &nonconnectable,
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 0);
    amulet_ble_clear_advertisement(&advertisement);
}

static void test_ambient_candidate_multiplicity(void)
{
    static const uint8_t field[] = {
        0x1a, 0xff,
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd, 0xc5,
    };
    amulet_ble_advertisement_t advertisement;
    uint8_t pair[sizeof(field) * 2];

    memcpy(pair, field, sizeof(field));
    memcpy(pair + sizeof(field), field, sizeof(field));
    CHECK(classify_advertisement_without_metadata(
        pair,
        sizeof(pair),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER);
    CHECK(advertisement.ambient_markers[0].multiplicity ==
          AMULET_BLE_CANDIDATE_MULTIPLICITY_REPEATED_IDENTICAL);
    amulet_ble_clear_advertisement(&advertisement);

    pair[sizeof(pair) - 1] = 0xc4;
    CHECK(classify_advertisement_without_metadata(
        pair,
        sizeof(pair),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].observation_kind ==
          AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC);
    CHECK(advertisement.ambient_markers[0].evidence ==
          AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT);
    CHECK(advertisement.ambient_markers[0].multiplicity ==
          AMULET_BLE_CANDIDATE_MULTIPLICITY_MULTIPLE_DISTINCT);
    amulet_ble_clear_advertisement(&advertisement);

    uint8_t overflow[sizeof(field) * (AMULET_BLE_MAX_AMBIENT_MARKERS + 1U)];
    for (size_t index = 0; index < AMULET_BLE_MAX_AMBIENT_MARKERS + 1U; index++) {
        memcpy(overflow + index * sizeof(field), field, sizeof(field));
    }
    CHECK(classify_advertisement_without_metadata(
        overflow,
        sizeof(overflow),
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_overflow);
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(advertisement.ambient_markers[0].multiplicity ==
          AMULET_BLE_CANDIDATE_MULTIPLICITY_OVERFLOW);
    amulet_ble_clear_advertisement(&advertisement);
}

static void test_complete_advertisement_dispatch(void)
{
    static const uint8_t tracker_and_peer[] = {
        0x08, 0xff, 0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
        0x11, 0x07,
        0x34, 0x36, 0x11, 0xaf, 0x30, 0x32, 0x05, 0xa9,
        0x44, 0x43, 0xae, 0xc8, 0x50, 0xba, 0xdb, 0x33,
    };
    static const uint8_t ibeacon_and_peer[] = {
        0x1a, 0xff,
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd, 0xc5,
        0x11, 0x06,
        0x34, 0x36, 0x11, 0xaf, 0x30, 0x32, 0x05, 0xa9,
        0x44, 0x43, 0xae, 0xc8, 0x50, 0xba, 0xdb, 0x33,
    };
    static const uint8_t repeated_manufacturer[] = {
        0x08, 0xff, 0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
        0x03, 0xff, 0x01, 0x02,
    };
    static const uint8_t reversed_manufacturer[] = {
        0x03, 0xff, 0x01, 0x02,
        0x08, 0xff, 0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
    };
    amulet_ble_advertisement_t advertisement;

    CHECK(classify_advertisement_without_metadata(
        tracker_and_peer,
        sizeof(tracker_and_peer),
        &advertisement
    ));
    CHECK(advertisement.has_test_tracker);
    CHECK(advertisement.has_peer_amulet);
    CHECK(advertisement.ambient_marker_count == 0);
    CHECK(amulet_ble_primary_action(&advertisement) ==
          AMULET_BLE_ACTION_TEST_TRACKER);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(classify_advertisement_without_metadata(
        ibeacon_and_peer,
        sizeof(ibeacon_and_peer),
        &advertisement
    ));
    CHECK(!advertisement.has_test_tracker);
    CHECK(advertisement.has_peer_amulet);
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(amulet_ble_primary_action(&advertisement) ==
          AMULET_BLE_ACTION_PEER_AMULET);
    CHECK(advertisement.ambient_markers[0].rule == AMULET_BLE_RULE_IBEACON);
    CHECK(advertisement.ambient_markers[0].identity[0] == 0x00);
    CHECK(advertisement.ambient_markers[0].identity[19] == 0xcd);
    CHECK(advertisement.ambient_markers[0].calibration_power == -59);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(classify_advertisement_without_metadata(
        ibeacon_and_peer,
        27,
        &advertisement
    ));
    CHECK(advertisement.ambient_marker_count == 1);
    CHECK(!advertisement.has_peer_amulet);
    CHECK(amulet_ble_primary_action(&advertisement) ==
          AMULET_BLE_ACTION_AMBIENT);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(classify_advertisement_without_metadata(
        repeated_manufacturer,
        sizeof(repeated_manufacturer),
        &advertisement
    ));
    CHECK(advertisement.has_test_tracker);
    amulet_ble_clear_advertisement(&advertisement);

    CHECK(classify_advertisement_without_metadata(
        reversed_manufacturer,
        sizeof(reversed_manufacturer),
        &advertisement
    ));
    CHECK(advertisement.has_test_tracker);
    amulet_ble_clear_advertisement(&advertisement);
}

static void test_malformed_complete_advertisements(void)
{
    static const uint8_t truncated_field[] = {0x05, 0xff, 0x4c};
    static const uint8_t zero_length_field[] = {0x00};
    static const uint8_t malformed_uuid128[] = {0x02, 0x07, 0x00};
    static const uint8_t malformed_manufacturer[] = {0x02, 0xff, 0x4c};
    static const uint8_t malformed_flags[] = {0x03, 0x01, 0x06, 0x00};
    static const uint8_t malformed_tk[] = {0x02, 0x10, 0x00};
    static const uint8_t malformed_oob_flags[] = {0x03, 0x11, 0x00, 0x00};
    static const uint8_t valid_ibeacon_then_malformed[] = {
        0x1a, 0xff,
        0x4c, 0x00, 0x02, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x12, 0x34, 0xab, 0xcd, 0xc5,
        0x00,
    };
    amulet_ble_advertisement_t advertisement;

    memset(&advertisement, 0xa5, sizeof(advertisement));
    CHECK(!classify_advertisement_without_metadata(
        truncated_field,
        sizeof(truncated_field),
        &advertisement
    ));
    const uint8_t zero[sizeof(advertisement)] = {0};
    CHECK(memcmp(&advertisement, zero, sizeof(advertisement)) == 0);
    CHECK(!classify_advertisement_without_metadata(
        zero_length_field,
        sizeof(zero_length_field),
        &advertisement
    ));
    CHECK(!classify_advertisement_without_metadata(
        malformed_uuid128,
        sizeof(malformed_uuid128),
        &advertisement
    ));
    CHECK(!classify_advertisement_without_metadata(
        malformed_manufacturer,
        sizeof(malformed_manufacturer),
        &advertisement
    ));
    CHECK(!classify_advertisement_without_metadata(
        malformed_flags,
        sizeof(malformed_flags),
        &advertisement
    ));
    CHECK(!classify_advertisement_without_metadata(
        malformed_tk,
        sizeof(malformed_tk),
        &advertisement
    ));
    CHECK(!classify_advertisement_without_metadata(
        malformed_oob_flags,
        sizeof(malformed_oob_flags),
        &advertisement
    ));
    memset(&advertisement, 0xa5, sizeof(advertisement));
    CHECK(!classify_advertisement_without_metadata(
        valid_ibeacon_then_malformed,
        sizeof(valid_ibeacon_then_malformed),
        &advertisement
    ));
    CHECK(memcmp(&advertisement, zero, sizeof(advertisement)) == 0);
    CHECK(classify_advertisement_without_metadata(NULL, 0, &advertisement));
    CHECK(!advertisement.has_test_tracker);
    CHECK(!advertisement.has_peer_amulet);
    CHECK(advertisement.ambient_marker_count == 0);
}

typedef struct {
    bool valid;
    uint8_t signals;
    uint8_t tracker_families;
} signal_result_t;

static signal_result_t classify_signals(const uint8_t *data, size_t length)
{
    amulet_ble_advertisement_t advertisement;
    signal_result_t result = {0};
    result.valid = classify_advertisement_without_metadata(data, length, &advertisement);
    result.signals = advertisement.signals;
    result.tracker_families = advertisement.tracker_families;
    amulet_ble_clear_advertisement(&advertisement);
    return result;
}

// Builds a 31-byte Apple offline-finding advertisement.
static size_t find_my_frame(uint8_t *out, uint8_t ad_length, uint8_t length_byte, uint8_t status)
{
    memset(out, 0xa5, 31);
    out[0] = ad_length;
    out[1] = 0xff;
    out[2] = 0x4c;
    out[3] = 0x00;
    out[4] = 0x12;
    out[5] = length_byte;
    out[6] = status;
    return (size_t)ad_length + 1U;
}

static bool is_separated(signal_result_t result, uint8_t family)
{
    return result.valid &&
           result.signals == AMULET_BLE_SIGNAL_TRACKER_SEPARATED &&
           result.tracker_families == family;
}

static bool has_no_signals(signal_result_t result)
{
    return result.valid && result.signals == 0 && result.tracker_families == 0;
}

static void test_find_my_signatures(void)
{
    uint8_t frame[31];

    // AirTag (type 1), third-party (type 2) and AirPods (type 3) when separated.
    CHECK(is_separated(classify_signals(frame, find_my_frame(frame, 0x1e, 0x19, 0x10)),
                       AMULET_BLE_TRACKER_FAMILY_FIND_MY));
    CHECK(is_separated(classify_signals(frame, find_my_frame(frame, 0x1e, 0x19, 0x20)),
                       AMULET_BLE_TRACKER_FAMILY_FIND_MY));
    CHECK(is_separated(classify_signals(frame, find_my_frame(frame, 0x1e, 0x19, 0x30)),
                       AMULET_BLE_TRACKER_FAMILY_FIND_MY));
    // Battery bits do not matter.
    CHECK(is_separated(classify_signals(frame, find_my_frame(frame, 0x1e, 0x19, 0xd0)),
                       AMULET_BLE_TRACKER_FAMILY_FIND_MY));

    // Phones and computers (type 0) never count.
    CHECK(has_no_signals(classify_signals(frame, find_my_frame(frame, 0x1e, 0x19, 0x00))));
    // Near owner: a different length byte, even at full size.
    CHECK(has_no_signals(classify_signals(frame, find_my_frame(frame, 0x1e, 0x02, 0x10))));
    // Near-owner short form.
    CHECK(has_no_signals(classify_signals(frame, find_my_frame(frame, 0x07, 0x02, 0x10))));
    // Truncated separated frame.
    CHECK(has_no_signals(classify_signals(frame, find_my_frame(frame, 0x1d, 0x19, 0x10))));
    // Wrong Apple message type (iBeacon is 0x02).
    find_my_frame(frame, 0x1e, 0x19, 0x10);
    frame[4] = 0x02;
    CHECK(has_no_signals(classify_signals(frame, sizeof(frame))));
    // Same payload under another company ID.
    find_my_frame(frame, 0x1e, 0x19, 0x10);
    frame[2] = 0x4d;
    CHECK(has_no_signals(classify_signals(frame, sizeof(frame))));
}

static void test_service_data_tracker_signatures(void)
{
    static const uint8_t smarttag_separated[] = {
        0x0f, 0x16, 0x5a, 0xfd, 0x13,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    };
    static const uint8_t smarttag_premature[] = {
        0x0f, 0x16, 0x5a, 0xfd, 0x11,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    };
    static const uint8_t smarttag_wrong_discriminator[] = {
        0x0f, 0x16, 0x5a, 0xfd, 0x23,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    };
    static const uint8_t smarttag_short[] = {
        0x0e, 0x16, 0x5a, 0xfd, 0x13,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    };
    uint8_t find_hub[26] = {0x18, 0x16, 0xaa, 0xfe, 0x41};
    uint8_t find_hub_flags[26] = {0x19, 0x16, 0xaa, 0xfe, 0x41};
    uint8_t find_hub_normal[26] = {0x18, 0x16, 0xaa, 0xfe, 0x40};
    uint8_t find_hub_short[26] = {0x17, 0x16, 0xaa, 0xfe, 0x41};
    static const uint8_t eddystone_uid[] = {
        0x17, 0x16, 0xaa, 0xfe, 0x00, 0xee,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00,
    };
    static const uint8_t dult_separated[] = {0x05, 0x16, 0xb2, 0xfc, 0x01, 0x00};
    static const uint8_t dult_near_owner[] = {0x05, 0x16, 0xb2, 0xfc, 0x01, 0x01};
    static const uint8_t dult_short[] = {0x04, 0x16, 0xb2, 0xfc, 0x01};
    static const uint8_t tile[] = {0x05, 0x16, 0xed, 0xfe, 0x02, 0x00};
    static const uint8_t tile_other_prefix[] = {0x05, 0x16, 0xed, 0xfe, 0x01, 0x00};

    CHECK(is_separated(classify_signals(smarttag_separated, sizeof(smarttag_separated)),
                       AMULET_BLE_TRACKER_FAMILY_SMARTTAG));
    CHECK(has_no_signals(classify_signals(smarttag_premature, sizeof(smarttag_premature))));
    CHECK(has_no_signals(classify_signals(smarttag_wrong_discriminator,
                                          sizeof(smarttag_wrong_discriminator))));
    CHECK(has_no_signals(classify_signals(smarttag_short, sizeof(smarttag_short))));

    CHECK(is_separated(classify_signals(find_hub, 25), AMULET_BLE_TRACKER_FAMILY_FIND_HUB));
    CHECK(is_separated(classify_signals(find_hub_flags, 26), AMULET_BLE_TRACKER_FAMILY_FIND_HUB));
    CHECK(has_no_signals(classify_signals(find_hub_normal, 25)));
    CHECK(has_no_signals(classify_signals(find_hub_short, 24)));
    CHECK(has_no_signals(classify_signals(eddystone_uid, sizeof(eddystone_uid))));

    CHECK(is_separated(classify_signals(dult_separated, sizeof(dult_separated)),
                       AMULET_BLE_TRACKER_FAMILY_DULT));
    CHECK(has_no_signals(classify_signals(dult_near_owner, sizeof(dult_near_owner))));
    CHECK(has_no_signals(classify_signals(dult_short, sizeof(dult_short))));

    CHECK(is_separated(classify_signals(tile, sizeof(tile)), AMULET_BLE_TRACKER_FAMILY_TILE));
    CHECK(has_no_signals(classify_signals(tile_other_prefix, sizeof(tile_other_prefix))));
}

static void test_smart_glasses_and_combined_signals(void)
{
    static const uint8_t meta[] = {0x03, 0xff, 0xab, 0x01};
    static const uint8_t meta_technologies[] = {0x05, 0xff, 0x8e, 0x05, 0x10, 0x20};
    static const uint8_t luxottica[] = {0x03, 0xff, 0x53, 0x0d};
    static const uint8_t snap[] = {0x03, 0xff, 0xc2, 0x03};
    static const uint8_t neighbor_id[] = {0x03, 0xff, 0xac, 0x01};
    static const uint8_t glasses_and_tile[] = {
        0x03, 0xff, 0xab, 0x01,
        0x05, 0x16, 0xed, 0xfe, 0x02, 0x00,
    };
    static const uint8_t malformed_with_tile[] = {
        0x05, 0x16, 0xed, 0xfe, 0x02, 0x00,
        0x09, 0xff, 0x01,
    };
    signal_result_t result;

    CHECK(classify_signals(meta, sizeof(meta)).signals == AMULET_BLE_SIGNAL_SMART_GLASSES);
    CHECK(classify_signals(meta_technologies, sizeof(meta_technologies)).signals ==
          AMULET_BLE_SIGNAL_SMART_GLASSES);
    CHECK(classify_signals(luxottica, sizeof(luxottica)).signals == AMULET_BLE_SIGNAL_SMART_GLASSES);
    CHECK(classify_signals(snap, sizeof(snap)).signals == AMULET_BLE_SIGNAL_SMART_GLASSES);
    CHECK(has_no_signals(classify_signals(neighbor_id, sizeof(neighbor_id))));

    result = classify_signals(glasses_and_tile, sizeof(glasses_and_tile));
    CHECK(result.valid);
    CHECK(result.signals == (AMULET_BLE_SIGNAL_SMART_GLASSES | AMULET_BLE_SIGNAL_TRACKER_SEPARATED));
    CHECK(result.tracker_families == AMULET_BLE_TRACKER_FAMILY_TILE);

    // A malformed advertisement yields nothing, even after a valid field.
    result = classify_signals(malformed_with_tile, sizeof(malformed_with_tile));
    CHECK(!result.valid);
    CHECK(result.signals == 0 && result.tracker_families == 0);
}

// Builds a 31-byte ASTM F3411 legacy advertisement carrying one message.
static size_t remote_id_frame(uint8_t *out, uint8_t ad_length, uint8_t app_code, uint8_t header)
{
    memset(out, 0x5a, 31);
    out[0] = ad_length;
    out[1] = 0x16;
    out[2] = 0xfa;
    out[3] = 0xff;
    out[4] = app_code;
    out[5] = 0x07; // message counter
    out[6] = header;
    return (size_t)ad_length + 1U;
}

static signal_result_t classify_signals_with_address(
    bool public_address,
    uint8_t prefix0,
    uint8_t prefix1,
    uint8_t prefix2
)
{
    static const uint8_t flags[] = {0x02, 0x01, 0x06};
    const amulet_ble_scan_metadata_t metadata = {
        .pdu_type = AMULET_BLE_PDU_TYPE_OTHER,
        .public_address = public_address,
        .address_prefix = {prefix0, prefix1, prefix2},
    };
    amulet_ble_advertisement_t advertisement;
    signal_result_t result = {0};
    result.valid = amulet_ble_classify_advertisement(
        flags,
        sizeof(flags),
        &metadata,
        &advertisement
    );
    result.signals = advertisement.signals;
    result.tracker_families = advertisement.tracker_families;
    amulet_ble_clear_advertisement(&advertisement);
    return result;
}

static void test_surveillance_signatures(void)
{
    uint8_t frame[31];
    signal_result_t result;

    // Basic ID, Location, Self-ID and Operator ID messages, protocol 0-2.
    CHECK(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x02)).signals ==
          AMULET_BLE_SIGNAL_DRONE_REMOTE_ID);
    CHECK(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x12)).signals ==
          AMULET_BLE_SIGNAL_DRONE_REMOTE_ID);
    CHECK(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x30)).signals ==
          AMULET_BLE_SIGNAL_DRONE_REMOTE_ID);
    CHECK(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x51)).signals ==
          AMULET_BLE_SIGNAL_DRONE_REMOTE_ID);

    // Wrong app code, unknown message type, message pack, newer protocol.
    CHECK(has_no_signals(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0c, 0x12))));
    CHECK(has_no_signals(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x62))));
    CHECK(has_no_signals(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0xf2))));
    CHECK(has_no_signals(classify_signals(frame, remote_id_frame(frame, 0x1e, 0x0d, 0x13))));
    // Truncated message.
    CHECK(has_no_signals(classify_signals(frame, remote_id_frame(frame, 0x1d, 0x0d, 0x12))));
    // Same payload under a neighboring UUID.
    remote_id_frame(frame, 0x1e, 0x0d, 0x12);
    frame[2] = 0xfb;
    CHECK(has_no_signals(classify_signals(frame, sizeof(frame))));

    // Axon's prefix on a public address.
    result = classify_signals_with_address(true, 0x00, 0x25, 0xdf);
    CHECK(result.valid && result.signals == AMULET_BLE_SIGNAL_POLICE_EQUIPMENT_MAKER);
    // A random address can match by chance and never counts.
    CHECK(has_no_signals(classify_signals_with_address(false, 0x00, 0x25, 0xdf)));
    // A neighboring prefix.
    CHECK(has_no_signals(classify_signals_with_address(true, 0x00, 0x25, 0xde)));
    // No metadata at all.
    static const uint8_t flags[] = {0x02, 0x01, 0x06};
    CHECK(has_no_signals(classify_signals(flags, sizeof(flags))));
}

static uint8_t classify_hunt_beacon(const uint8_t *data, size_t length, bool *valid)
{
    amulet_ble_advertisement_t advertisement;
    *valid = classify_advertisement_without_metadata(data, length, &advertisement);
    uint8_t beacon = advertisement.hunt_beacon;
    amulet_ble_clear_advertisement(&advertisement);
    return beacon;
}

static void test_hunt_beacon(void)
{
    static const uint8_t beacon_two[] = {
        0x02, 0x01, 0x06,
        0x06, 0xff, 0xff, 0xff, 0xa3, 0x01, 0x02,
    };
    static const uint8_t beacon_eight[] = {0x06, 0xff, 0xff, 0xff, 0xa3, 0x01, 0x08};
    static const uint8_t beacon_zero[] = {0x06, 0xff, 0xff, 0xff, 0xa3, 0x01, 0x00};
    static const uint8_t beacon_nine[] = {0x06, 0xff, 0xff, 0xff, 0xa3, 0x01, 0x09};
    static const uint8_t wrong_type[] = {0x06, 0xff, 0xff, 0xff, 0xa1, 0x01, 0x02};
    static const uint8_t wrong_version[] = {0x06, 0xff, 0xff, 0xff, 0xa3, 0x02, 0x02};
    static const uint8_t wrong_company[] = {0x06, 0xff, 0xfe, 0xff, 0xa3, 0x01, 0x02};
    static const uint8_t too_long[] = {0x07, 0xff, 0xff, 0xff, 0xa3, 0x01, 0x02, 0x00};
    static const uint8_t too_short[] = {0x05, 0xff, 0xff, 0xff, 0xa3, 0x01};
    static const uint8_t in_service_data[] = {0x06, 0x16, 0xff, 0xff, 0xa3, 0x01, 0x02};
    bool valid;

    CHECK(classify_hunt_beacon(beacon_two, sizeof(beacon_two), &valid) == 2 && valid);
    CHECK(classify_hunt_beacon(beacon_eight, sizeof(beacon_eight), &valid) == 8 && valid);
    CHECK(classify_hunt_beacon(beacon_zero, sizeof(beacon_zero), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(beacon_nine, sizeof(beacon_nine), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(wrong_type, sizeof(wrong_type), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(wrong_version, sizeof(wrong_version), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(wrong_company, sizeof(wrong_company), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(too_long, sizeof(too_long), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(too_short, sizeof(too_short), &valid) == 0 && valid);
    CHECK(classify_hunt_beacon(in_service_data, sizeof(in_service_data), &valid) == 0 && valid);

    // A hunt beacon is not the workshop tracker fixture and raises no signal.
    amulet_ble_advertisement_t advertisement;
    CHECK(classify_advertisement_without_metadata(beacon_two, sizeof(beacon_two), &advertisement));
    CHECK(!advertisement.has_test_tracker && advertisement.signals == 0);
    CHECK(amulet_ble_primary_action(&advertisement) == AMULET_BLE_ACTION_NONE);
    amulet_ble_clear_advertisement(&advertisement);
}

int main(void)
{
    test_exact_tracker_fixture();
    test_tracker_near_matches();
    test_exact_peer_uuid();
    test_peer_uuid_near_matches();
    test_ibeacon_fixture();
    test_ibeacon_near_matches();
    test_ibeacon_calibration_boundaries();
    test_altbeacon_fixture_and_evidence();
    test_altbeacon_near_matches();
    test_altbeacon_advertisement_dispatch();
    test_ambient_candidate_multiplicity();
    test_complete_advertisement_dispatch();
    test_malformed_complete_advertisements();
    test_find_my_signatures();
    test_service_data_tracker_signatures();
    test_smart_glasses_and_combined_signals();
    test_surveillance_signatures();
    test_hunt_beacon();

    if (failure_count != 0) {
        fprintf(stderr, "%d classifier fixture check(s) failed\n", failure_count);
        return 1;
    }

    puts("BLE classifier fixtures passed");
    return 0;
}
