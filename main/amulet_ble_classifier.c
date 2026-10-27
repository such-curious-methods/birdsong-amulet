#include "amulet_ble_classifier.h"

#include <limits.h>
#include <string.h>

#include "amulet_ble_company_ids_generated.h"

static const uint8_t TEST_TRACKER_PAYLOAD[] = {
    0xff, 0xff, 'T', 'R', 'K', 'R', 0x01,
};
static const uint8_t PEER_SERVICE_UUID[] = {
    AMULET_PEER_SERVICE_UUID_BYTES,
};
static const uint8_t IBEACON_PREFIX[] = {0x4c, 0x00, 0x02, 0x15};
static const uint8_t ALTBEACON_CODE[] = {0xbe, 0xac};

#define IBEACON_MANUFACTURER_DATA_LENGTH 25U
#define IBEACON_IDENTITY_OFFSET 4U
#define IBEACON_CALIBRATION_OFFSET 24U
#define ALTBEACON_MANUFACTURER_DATA_LENGTH 26U
#define ALTBEACON_CODE_OFFSET 2U
#define ALTBEACON_BEACON_ID_OFFSET 4U
#define ALTBEACON_REFERENCE_RSSI_OFFSET 24U
#define ALTBEACON_RESERVED_OFFSET 25U
#define LEGACY_ADVERTISING_DATA_MAX_LENGTH 31U
#define AD_TYPE_INCOMPLETE_UUID128 0x06
#define AD_TYPE_COMPLETE_UUID128 0x07
#define AD_TYPE_MANUFACTURER_DATA 0xff
#define AD_TYPE_SERVICE_DATA_UUID16 0x16

// Provisional signatures; byte positions include the company ID or UUID.
// Sources and caveats: tracker-signatures.org.
#define APPLE_COMPANY_ID 0x004cU
#define FIND_MY_TYPE 0x12U
#define FIND_MY_SEPARATED_LENGTH 0x19U
#define FIND_MY_SEPARATED_DATA_LENGTH 29U
#define FIND_MY_DEVICE_TYPE_SHIFT 4U
#define FIND_MY_DEVICE_TYPE_MASK 0x03U
#define FIND_MY_DEVICE_TYPE_APPLE_DEVICE 0U
#define SMARTTAG_UUID 0xfd5aU
#define SMARTTAG_MIN_DATA_LENGTH 14U
#define SMARTTAG_DISCRIMINATOR_MASK 0xf8U
#define SMARTTAG_DISCRIMINATOR_VALUE 0x10U
#define SMARTTAG_STATE_MASK 0x07U
#define SMARTTAG_STATE_SEPARATED 0x03U
#define FIND_HUB_UUID 0xfeaaU
#define FIND_HUB_FRAME_SEPARATED 0x41U
#define FIND_HUB_DATA_LENGTH 23U
#define FIND_HUB_DATA_LENGTH_WITH_FLAGS 24U
#define DULT_UUID 0xfcb2U
#define DULT_MIN_DATA_LENGTH 4U
#define DULT_NEAR_OWNER_BIT 0x01U
#define TILE_UUID 0xfeedU
#define TILE_MIN_DATA_LENGTH 4U
// ASTM F3411 legacy advertising: UUID, app code 0x0D, message counter, then
// one 25-byte message whose high nibble is the type and low nibble the version.
#define REMOTE_ID_UUID 0xfffaU
#define REMOTE_ID_APP_CODE 0x0dU
#define REMOTE_ID_DATA_LENGTH 29U
#define REMOTE_ID_MESSAGE_OFFSET 4U
#define REMOTE_ID_MAX_MESSAGE_TYPE 0x05U
#define REMOTE_ID_MAX_PROTOCOL_VERSION 0x02U

static const uint16_t SMART_GLASSES_COMPANY_IDS[] = {
    0x01ab, // Meta Platforms, Inc.
    0x058e, // Meta Platforms Technologies, LLC
    0x0d53, // Luxottica Group S.p.A
    0x03c2, // Snapchat Inc
};

// Vendor tier (tracker-detection-roadmap.org): IEEE MA-L prefixes registered
// to single-purpose police-equipment makers, matched on public addresses only.
static const uint8_t POLICE_EQUIPMENT_PREFIXES[][AMULET_BLE_ADDRESS_PREFIX_LENGTH] = {
    {0x00, 0x25, 0xdf}, // Axon Enterprise, Inc. (body cameras, TASERs)
};

static bool ad_field_shape_is_valid(uint8_t type, size_t length)
{
    switch (type) {
    case 0x01: // Flags
    case 0x0a: // TX power level
    case 0x11: // Security Manager out-of-band flags
    case 0x1c: // LE role
        return length == 1;
    case 0x02: // Incomplete 16-bit UUIDs
    case 0x03: // Complete 16-bit UUIDs
    case 0x14: // Solicited 16-bit UUIDs
        return length % 2 == 0;
    case 0x04: // Incomplete 32-bit UUIDs
    case 0x05: // Complete 32-bit UUIDs
    case 0x1f: // Solicited 32-bit UUIDs
        return length % 4 == 0;
    case AD_TYPE_INCOMPLETE_UUID128:
    case AD_TYPE_COMPLETE_UUID128:
    case 0x15: // Solicited 128-bit UUIDs
        return length % 16 == 0;
    case 0x12: // Peripheral connection interval range
        return length == 4;
    case 0x10: // Security Manager temporary key
        return length == 16;
    case 0x16: // Service data with 16-bit UUID
        return length >= 2;
    case 0x17: // Public target addresses
    case 0x18: // Random target addresses
        return length % 6 == 0;
    case 0x19: // Appearance
    case 0x1a: // Advertising interval
        return length == 2;
    case 0x1b: // LE Bluetooth device address
        return length == 7;
    case 0x20: // Service data with 32-bit UUID
        return length >= 4;
    case 0x21: // Service data with 128-bit UUID
        return length >= 16;
    case AD_TYPE_MANUFACTURER_DATA:
        return length >= 2;
    default:
        return true;
    }
}

_Static_assert(
    sizeof(PEER_SERVICE_UUID) == AMULET_PEER_SERVICE_UUID_LENGTH,
    "peer service UUID length must remain 128 bits"
);
_Static_assert(
    sizeof(AMULET_BLE_ASSIGNED_COMPANY_ID_BITS) ==
        AMULET_BLE_ASSIGNED_COMPANY_ID_MAX / 8U + 1U,
    "company identifier bitset must cover its declared maximum"
);

static void clear_sensitive(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length > 0) {
        *bytes++ = 0;
        length--;
    }
}

static amulet_ble_ambient_evidence_t altbeacon_ambient_evidence(
    amulet_ble_altbeacon_evidence_t evidence
);

static amulet_ble_classification_t no_classification(void)
{
    return (amulet_ble_classification_t) {
        .rule = AMULET_BLE_RULE_NONE,
        .category = AMULET_BLE_CATEGORY_NONE,
        .confidence = AMULET_BLE_CONFIDENCE_NONE,
        .ambient_evidence = AMULET_BLE_AMBIENT_EVIDENCE_NONE,
    };
}

static bool company_id_is_assigned(uint16_t company_id)
{
    return company_id <= AMULET_BLE_ASSIGNED_COMPANY_ID_MAX &&
           (AMULET_BLE_ASSIGNED_COMPANY_ID_BITS[company_id / 8U] &
            (uint8_t)(1U << (company_id % 8U))) != 0;
}

amulet_ble_classification_t amulet_ble_classify_manufacturer_data(
    const uint8_t *data,
    size_t length
)
{
    if (data != NULL && length == sizeof(TEST_TRACKER_PAYLOAD) &&
        memcmp(data, TEST_TRACKER_PAYLOAD, sizeof(TEST_TRACKER_PAYLOAD)) == 0) {
        return (amulet_ble_classification_t) {
            .rule = AMULET_BLE_RULE_TEST_TRACKER,
            .category = AMULET_BLE_CATEGORY_WORKSHOP_FIXTURE,
            .confidence = AMULET_BLE_CONFIDENCE_EXACT,
        };
    }

    if (amulet_ble_parse_ibeacon(data, length, NULL)) {
        return (amulet_ble_classification_t) {
            .rule = AMULET_BLE_RULE_IBEACON,
            .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
            .confidence = AMULET_BLE_CONFIDENCE_STRUCTURAL,
            .ambient_evidence = AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT,
        };
    }

    amulet_ble_altbeacon_t altbeacon;
    bool is_altbeacon = amulet_ble_parse_altbeacon(
            data,
            length,
            AMULET_BLE_PDU_TYPE_UNAVAILABLE,
            &altbeacon
        );
    if (is_altbeacon) {
        amulet_ble_classification_t classification = {
            .rule = AMULET_BLE_RULE_ALTBEACON,
            .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
            .confidence = AMULET_BLE_CONFIDENCE_STRUCTURAL,
            .ambient_evidence = altbeacon_ambient_evidence(altbeacon.evidence),
        };
        clear_sensitive(&altbeacon, sizeof(altbeacon));
        return classification;
    }
    clear_sensitive(&altbeacon, sizeof(altbeacon));

    return no_classification();
}

amulet_ble_classification_t amulet_ble_classify_service_uuid128(
    const uint8_t *uuid,
    size_t length
)
{
    if (uuid == NULL || length != sizeof(PEER_SERVICE_UUID) ||
        memcmp(uuid, PEER_SERVICE_UUID, sizeof(PEER_SERVICE_UUID)) != 0) {
        return no_classification();
    }

    return (amulet_ble_classification_t) {
        .rule = AMULET_BLE_RULE_PEER_AMULET,
        .category = AMULET_BLE_CATEGORY_LOCAL_PROTOCOL,
        .confidence = AMULET_BLE_CONFIDENCE_EXACT,
        .ambient_evidence = AMULET_BLE_AMBIENT_EVIDENCE_NONE,
    };
}

bool amulet_ble_parse_ibeacon(
    const uint8_t *data,
    size_t length,
    amulet_ble_ibeacon_t *ibeacon
)
{
    if (ibeacon != NULL) {
        memset(ibeacon, 0, sizeof(*ibeacon));
    }
    if (data == NULL || length != IBEACON_MANUFACTURER_DATA_LENGTH ||
        memcmp(data, IBEACON_PREFIX, sizeof(IBEACON_PREFIX)) != 0) {
        return false;
    }

    if (ibeacon != NULL) {
        memcpy(
            ibeacon->identity,
            data + IBEACON_IDENTITY_OFFSET,
            sizeof(ibeacon->identity)
        );
        memcpy(
            &ibeacon->calibration_power,
            data + IBEACON_CALIBRATION_OFFSET,
            sizeof(ibeacon->calibration_power)
        );
    }
    return true;
}

bool amulet_ble_parse_altbeacon(
    const uint8_t *data,
    size_t length,
    amulet_ble_pdu_type_t pdu_type,
    amulet_ble_altbeacon_t *altbeacon
)
{
    if (altbeacon != NULL) {
        memset(altbeacon, 0, sizeof(*altbeacon));
    }
    if (data == NULL || length != ALTBEACON_MANUFACTURER_DATA_LENGTH ||
        memcmp(
            data + ALTBEACON_CODE_OFFSET,
            ALTBEACON_CODE,
            sizeof(ALTBEACON_CODE)
        ) != 0) {
        return false;
    }

    int8_t reference_rssi;
    memcpy(
        &reference_rssi,
        data + ALTBEACON_REFERENCE_RSSI_OFFSET,
        sizeof(reference_rssi)
    );
    if (reference_rssi == INT8_MIN || reference_rssi > 0) {
        return false;
    }

    uint16_t company_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    amulet_ble_altbeacon_evidence_t evidence = AMULET_BLE_ALTBEACON_SHAPED;
    if (company_id_is_assigned(company_id)) {
        switch (pdu_type) {
        case AMULET_BLE_PDU_TYPE_UNAVAILABLE:
            evidence = AMULET_BLE_ALTBEACON_PAYLOAD_MATCH;
            break;
        case AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND:
            evidence = AMULET_BLE_ALTBEACON_STRICT_V1;
            break;
        case AMULET_BLE_PDU_TYPE_OTHER:
        default:
            evidence = AMULET_BLE_ALTBEACON_ENVELOPE_CONFLICT;
            break;
        }
    }

    if (altbeacon != NULL) {
        memcpy(
            altbeacon->beacon_id,
            data + ALTBEACON_BEACON_ID_OFFSET,
            sizeof(altbeacon->beacon_id)
        );
        altbeacon->company_id = company_id;
        altbeacon->reference_rssi = reference_rssi;
        altbeacon->reserved = data[ALTBEACON_RESERVED_OFFSET];
        altbeacon->evidence = evidence;
    }
    return true;
}

bool amulet_ble_parse_hunt_beacon(
    const uint8_t *data,
    size_t length,
    uint8_t *beacon
)
{
    if (data == NULL || length != AMULET_HUNT_BEACON_DATA_LENGTH ||
        data[0] != 0xffU || data[1] != 0xffU ||
        data[2] != AMULET_HUNT_BEACON_TYPE ||
        data[3] != AMULET_HUNT_BEACON_VERSION ||
        data[4] == 0 || data[4] > AMULET_HUNT_BEACON_MAX_NUMBER) {
        return false;
    }
    if (beacon != NULL) {
        *beacon = data[4];
    }
    return true;
}

bool amulet_ble_altbeacon_evidence_is_dispatchable(
    amulet_ble_altbeacon_evidence_t evidence
)
{
    return evidence == AMULET_BLE_ALTBEACON_STRICT_V1 ||
           evidence == AMULET_BLE_ALTBEACON_PAYLOAD_MATCH;
}

static amulet_ble_ambient_evidence_t altbeacon_ambient_evidence(
    amulet_ble_altbeacon_evidence_t evidence
)
{
    switch (evidence) {
    case AMULET_BLE_ALTBEACON_STRICT_V1:
        return AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1;
    case AMULET_BLE_ALTBEACON_PAYLOAD_MATCH:
        return AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH;
    case AMULET_BLE_ALTBEACON_ENVELOPE_CONFLICT:
        return AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT;
    case AMULET_BLE_ALTBEACON_SHAPED:
        return AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_SHAPED;
    case AMULET_BLE_ALTBEACON_EVIDENCE_NONE:
    default:
        return AMULET_BLE_AMBIENT_EVIDENCE_NONE;
    }
}

static void set_marker_overflow(amulet_ble_advertisement_t *advertisement)
{
    clear_sensitive(
        advertisement->ambient_markers,
        sizeof(advertisement->ambient_markers)
    );
    advertisement->ambient_markers[0] = (amulet_ble_ambient_marker_t) {
        .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        .observation_kind = AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC,
        .multiplicity = AMULET_BLE_CANDIDATE_MULTIPLICITY_OVERFLOW,
    };
    advertisement->ambient_marker_count = 1;
    advertisement->ambient_marker_overflow = true;
}

typedef struct {
    const uint8_t *data;
    size_t length;
    amulet_ble_rule_t rule;
    amulet_ble_ambient_observation_kind_t observation_kind;
} ambient_candidate_view_t;

typedef struct {
    ambient_candidate_view_t candidates[AMULET_BLE_MAX_AMBIENT_MARKERS];
    uint8_t count;
} ambient_candidate_set_t;

static amulet_ble_ambient_marker_t *find_logical_marker(
    amulet_ble_advertisement_t *advertisement,
    amulet_ble_rule_t rule
)
{
    for (uint8_t index = 0; index < advertisement->ambient_marker_count; index++) {
        amulet_ble_ambient_marker_t *marker =
            &advertisement->ambient_markers[index];
        if (marker->rule == rule &&
            marker->observation_kind ==
                AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER) {
            return marker;
        }
    }
    return NULL;
}

static bool rule_has_multiplicity_conflict(
    const amulet_ble_advertisement_t *advertisement,
    amulet_ble_rule_t rule
)
{
    for (uint8_t index = 0; index < advertisement->ambient_marker_count; index++) {
        const amulet_ble_ambient_marker_t *marker =
            &advertisement->ambient_markers[index];
        if (marker->rule == rule &&
            marker->multiplicity ==
                AMULET_BLE_CANDIDATE_MULTIPLICITY_MULTIPLE_DISTINCT) {
            return true;
        }
    }
    return false;
}

static void suppress_distinct_rule_candidates(
    amulet_ble_advertisement_t *advertisement,
    const amulet_ble_ambient_marker_t *candidate
)
{
    uint8_t write_index = 0;
    for (uint8_t read_index = 0;
         read_index < advertisement->ambient_marker_count;
         read_index++) {
        amulet_ble_ambient_marker_t *marker =
            &advertisement->ambient_markers[read_index];
        if (marker->rule != candidate->rule ||
            marker->observation_kind !=
                AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER) {
            if (write_index != read_index) {
                advertisement->ambient_markers[write_index] = *marker;
            }
            write_index++;
        }
    }
    for (uint8_t index = write_index;
         index < advertisement->ambient_marker_count;
         index++) {
        clear_sensitive(
            &advertisement->ambient_markers[index],
            sizeof(advertisement->ambient_markers[index])
        );
    }
    advertisement->ambient_marker_count = write_index;
    advertisement->ambient_markers[advertisement->ambient_marker_count++] =
        (amulet_ble_ambient_marker_t) {
            .rule = candidate->rule,
            .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
            .observation_kind = AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC,
            .evidence = candidate->evidence,
            .multiplicity =
                AMULET_BLE_CANDIDATE_MULTIPLICITY_MULTIPLE_DISTINCT,
        };
}

static void append_ambient_marker(
    amulet_ble_advertisement_t *advertisement,
    ambient_candidate_set_t *candidate_set,
    const amulet_ble_ambient_marker_t *marker,
    const uint8_t *candidate_data,
    size_t candidate_length
)
{
    if (advertisement->ambient_marker_overflow) {
        return;
    }
    if (candidate_set->count >= AMULET_BLE_MAX_AMBIENT_MARKERS) {
        set_marker_overflow(advertisement);
        return;
    }
    bool repeated_identical = false;
    bool multiple_distinct = false;
    for (uint8_t index = 0; index < candidate_set->count; index++) {
        const ambient_candidate_view_t *existing =
            &candidate_set->candidates[index];
        if (existing->rule == marker->rule &&
            existing->observation_kind ==
                AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER &&
            marker->observation_kind ==
                AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER) {
            if (existing->length == candidate_length &&
                memcmp(existing->data, candidate_data, candidate_length) == 0) {
                repeated_identical = true;
            } else {
                multiple_distinct = true;
            }
        }
    }

    candidate_set->candidates[candidate_set->count++] =
        (ambient_candidate_view_t) {
            .data = candidate_data,
            .length = candidate_length,
            .rule = marker->rule,
            .observation_kind = marker->observation_kind,
        };

    if (rule_has_multiplicity_conflict(advertisement, marker->rule)) {
        return;
    }
    if (multiple_distinct) {
        suppress_distinct_rule_candidates(advertisement, marker);
        return;
    }
    if (repeated_identical) {
        amulet_ble_ambient_marker_t *existing = find_logical_marker(
            advertisement,
            marker->rule
        );
        if (existing != NULL) {
            existing->multiplicity =
                AMULET_BLE_CANDIDATE_MULTIPLICITY_REPEATED_IDENTICAL;
        }
        return;
    }
    for (uint8_t index = 0; index < advertisement->ambient_marker_count; index++) {
        const amulet_ble_ambient_marker_t *existing =
            &advertisement->ambient_markers[index];
        if (existing->rule == marker->rule &&
            existing->observation_kind == marker->observation_kind &&
            existing->evidence == marker->evidence) {
            return;
        }
    }
    if (advertisement->ambient_marker_count >= AMULET_BLE_MAX_AMBIENT_MARKERS) {
        set_marker_overflow(advertisement);
        return;
    }

    amulet_ble_ambient_marker_t stored = *marker;
    stored.multiplicity = AMULET_BLE_CANDIDATE_MULTIPLICITY_ONE;
    advertisement->ambient_markers[advertisement->ambient_marker_count++] = stored;
    clear_sensitive(&stored, sizeof(stored));
}

static amulet_ble_ambient_marker_t ibeacon_marker(
    const amulet_ble_ibeacon_t *ibeacon
)
{
    amulet_ble_ambient_marker_t marker = {
        .identity_length = AMULET_IBEACON_IDENTITY_LENGTH,
        .rule = AMULET_BLE_RULE_IBEACON,
        .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        .confidence = AMULET_BLE_CONFIDENCE_STRUCTURAL,
        .observation_kind = AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER,
        .evidence = AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT,
        .calibration_power = ibeacon->calibration_power,
        .calibration_power_valid = true,
    };
    memcpy(marker.identity, ibeacon->identity, sizeof(ibeacon->identity));
    return marker;
}

static amulet_ble_ambient_marker_t altbeacon_marker(
    const amulet_ble_altbeacon_t *altbeacon
)
{
    bool dispatchable = amulet_ble_altbeacon_evidence_is_dispatchable(
        altbeacon->evidence
    );
    amulet_ble_ambient_marker_t marker = {
        .identity_length = dispatchable ? AMULET_ALTBEACON_IDENTITY_LENGTH : 0,
        .rule = AMULET_BLE_RULE_ALTBEACON,
        .category = AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
        .confidence = AMULET_BLE_CONFIDENCE_STRUCTURAL,
        .observation_kind = dispatchable
            ? AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER
            : AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC,
        .evidence = altbeacon_ambient_evidence(altbeacon->evidence),
        .calibration_power = altbeacon->reference_rssi,
        .calibration_power_valid = dispatchable,
    };
    if (dispatchable) {
        marker.identity[0] = (uint8_t)(altbeacon->company_id & 0xffU);
        marker.identity[1] = (uint8_t)(altbeacon->company_id >> 8);
        memcpy(
            marker.identity + 2,
            altbeacon->beacon_id,
            sizeof(altbeacon->beacon_id)
        );
    }
    return marker;
}

void amulet_ble_clear_advertisement(amulet_ble_advertisement_t *advertisement)
{
    if (advertisement == NULL) {
        return;
    }

    clear_sensitive(advertisement, sizeof(*advertisement));
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static void add_separated_tracker(
    amulet_ble_advertisement_t *result,
    amulet_ble_tracker_family_t family
)
{
    result->signals |= AMULET_BLE_SIGNAL_TRACKER_SEPARATED;
    result->tracker_families |= family;
}

static void match_provisional_manufacturer_data(
    const uint8_t *data,
    size_t length,
    amulet_ble_advertisement_t *result
)
{
    uint16_t company_id = read_le16(data);
    for (size_t index = 0;
         index < sizeof(SMART_GLASSES_COMPANY_IDS) / sizeof(SMART_GLASSES_COMPANY_IDS[0]);
         index++) {
        if (company_id == SMART_GLASSES_COMPANY_IDS[index]) {
            result->signals |= AMULET_BLE_SIGNAL_SMART_GLASSES;
        }
    }

    // Apple offline finding: only the full-length separated form counts, and
    // only for AirTags, AirPods and third-party Find My accessories, never
    // for phones or computers (device type 0). AirGuard's extra status mask
    // (0x18 == 0x10) would exclude third-party accessories (type 2), so it is
    // left out until captures settle the status bits.
    if (company_id == APPLE_COMPANY_ID &&
        length == FIND_MY_SEPARATED_DATA_LENGTH &&
        data[2] == FIND_MY_TYPE &&
        data[3] == FIND_MY_SEPARATED_LENGTH &&
        ((data[4] >> FIND_MY_DEVICE_TYPE_SHIFT) & FIND_MY_DEVICE_TYPE_MASK) !=
            FIND_MY_DEVICE_TYPE_APPLE_DEVICE) {
        add_separated_tracker(result, AMULET_BLE_TRACKER_FAMILY_FIND_MY);
    }
}

static void match_provisional_service_data(
    const uint8_t *data,
    size_t length,
    amulet_ble_advertisement_t *result
)
{
    switch (read_le16(data)) {
    case SMARTTAG_UUID:
        if (length >= SMARTTAG_MIN_DATA_LENGTH &&
            (data[2] & SMARTTAG_DISCRIMINATOR_MASK) == SMARTTAG_DISCRIMINATOR_VALUE &&
            (data[2] & SMARTTAG_STATE_MASK) == SMARTTAG_STATE_SEPARATED) {
            add_separated_tracker(result, AMULET_BLE_TRACKER_FAMILY_SMARTTAG);
        }
        break;
    case FIND_HUB_UUID:
        // Frame 0x41 is unwanted-tracking-protection mode; the exact lengths
        // keep Eddystone frames, which share this UUID, from matching.
        if ((length == FIND_HUB_DATA_LENGTH || length == FIND_HUB_DATA_LENGTH_WITH_FLAGS) &&
            data[2] == FIND_HUB_FRAME_SEPARATED) {
            add_separated_tracker(result, AMULET_BLE_TRACKER_FAMILY_FIND_HUB);
        }
        break;
    case DULT_UUID:
        if (length >= DULT_MIN_DATA_LENGTH && (data[3] & DULT_NEAR_OWNER_BIT) == 0) {
            add_separated_tracker(result, AMULET_BLE_TRACKER_FAMILY_DULT);
        }
        break;
    case TILE_UUID:
        if (length >= TILE_MIN_DATA_LENGTH && data[2] == 0x02 && data[3] == 0x00) {
            add_separated_tracker(result, AMULET_BLE_TRACKER_FAMILY_TILE);
        }
        break;
    case REMOTE_ID_UUID:
        // Legacy advertising carries exactly one message; message packs
        // (type 0xF) need extended advertising, which the ESP32 cannot hear.
        if (length == REMOTE_ID_DATA_LENGTH &&
            data[2] == REMOTE_ID_APP_CODE &&
            (data[REMOTE_ID_MESSAGE_OFFSET] >> 4) <= REMOTE_ID_MAX_MESSAGE_TYPE &&
            (data[REMOTE_ID_MESSAGE_OFFSET] & 0x0fU) <= REMOTE_ID_MAX_PROTOCOL_VERSION) {
            result->signals |= AMULET_BLE_SIGNAL_DRONE_REMOTE_ID;
        }
        break;
    default:
        break;
    }
}

static void match_police_equipment_maker(
    const amulet_ble_scan_metadata_t *metadata,
    amulet_ble_advertisement_t *result
)
{
    if (metadata == NULL || !metadata->public_address) {
        return;
    }
    for (size_t index = 0;
         index < sizeof(POLICE_EQUIPMENT_PREFIXES) / sizeof(POLICE_EQUIPMENT_PREFIXES[0]);
         index++) {
        if (memcmp(metadata->address_prefix,
                   POLICE_EQUIPMENT_PREFIXES[index],
                   AMULET_BLE_ADDRESS_PREFIX_LENGTH) == 0) {
            result->signals |= AMULET_BLE_SIGNAL_POLICE_EQUIPMENT_MAKER;
        }
    }
}

bool amulet_ble_classify_advertisement(
    const uint8_t *data,
    size_t length,
    const amulet_ble_scan_metadata_t *metadata,
    amulet_ble_advertisement_t *advertisement
)
{
    if (advertisement == NULL) {
        return false;
    }
    amulet_ble_clear_advertisement(advertisement);
    if (data == NULL && length > 0) {
        return false;
    }

    amulet_ble_advertisement_t result = {0};
    ambient_candidate_set_t candidate_set = {0};
    amulet_ble_pdu_type_t pdu_type = metadata != NULL
        ? metadata->pdu_type
        : AMULET_BLE_PDU_TYPE_UNAVAILABLE;
    if (length > LEGACY_ADVERTISING_DATA_MAX_LENGTH) {
        pdu_type = AMULET_BLE_PDU_TYPE_OTHER;
    }
    size_t offset = 0;
    while (offset < length) {
        uint8_t field_length = data[offset];
        size_t total_length = (size_t)field_length + 1;
        if (field_length == 0 || total_length > length - offset) {
            amulet_ble_clear_advertisement(advertisement);
            amulet_ble_clear_advertisement(&result);
            return false;
        }

        uint8_t type = data[offset + 1];
        const uint8_t *field_data = data + offset + 2;
        size_t field_data_length = field_length - 1U;
        if (!ad_field_shape_is_valid(type, field_data_length)) {
            amulet_ble_clear_advertisement(advertisement);
            amulet_ble_clear_advertisement(&result);
            return false;
        }
        if (type == AD_TYPE_MANUFACTURER_DATA) {
            match_provisional_manufacturer_data(field_data, field_data_length, &result);
            uint8_t hunt_beacon;
            if (amulet_ble_parse_hunt_beacon(field_data, field_data_length, &hunt_beacon)) {
                result.hunt_beacon = hunt_beacon;
            }
            amulet_ble_classification_t classification =
                amulet_ble_classify_manufacturer_data(
                    field_data,
                    field_data_length
                );
            if (classification.rule == AMULET_BLE_RULE_TEST_TRACKER) {
                result.has_test_tracker = true;
            } else if (classification.rule == AMULET_BLE_RULE_IBEACON) {
                amulet_ble_ibeacon_t ibeacon;
                if (amulet_ble_parse_ibeacon(
                    field_data,
                    field_data_length,
                    &ibeacon
                )) {
                    amulet_ble_ambient_marker_t marker = ibeacon_marker(&ibeacon);
                    append_ambient_marker(
                        &result,
                        &candidate_set,
                        &marker,
                        field_data,
                        field_data_length
                    );
                    clear_sensitive(&marker, sizeof(marker));
                }
                clear_sensitive(&ibeacon, sizeof(ibeacon));
            } else if (classification.rule == AMULET_BLE_RULE_ALTBEACON) {
                amulet_ble_altbeacon_t altbeacon;
                if (amulet_ble_parse_altbeacon(
                        field_data,
                        field_data_length,
                        pdu_type,
                        &altbeacon
                    )) {
                    amulet_ble_ambient_marker_t marker = altbeacon_marker(&altbeacon);
                    append_ambient_marker(
                        &result,
                        &candidate_set,
                        &marker,
                        field_data,
                        field_data_length
                    );
                    clear_sensitive(&marker, sizeof(marker));
                }
                clear_sensitive(&altbeacon, sizeof(altbeacon));
            }
        } else if (type == AD_TYPE_SERVICE_DATA_UUID16) {
            match_provisional_service_data(field_data, field_data_length, &result);
        } else if (type == AD_TYPE_INCOMPLETE_UUID128 ||
                   type == AD_TYPE_COMPLETE_UUID128) {
            if (field_data_length % AMULET_PEER_SERVICE_UUID_LENGTH != 0) {
                amulet_ble_clear_advertisement(advertisement);
                amulet_ble_clear_advertisement(&result);
                return false;
            }
            for (size_t uuid_offset = 0;
                 uuid_offset < field_data_length;
                 uuid_offset += AMULET_PEER_SERVICE_UUID_LENGTH) {
                amulet_ble_classification_t classification =
                    amulet_ble_classify_service_uuid128(
                        field_data + uuid_offset,
                        AMULET_PEER_SERVICE_UUID_LENGTH
                    );
                if (classification.rule == AMULET_BLE_RULE_PEER_AMULET) {
                    result.has_peer_amulet = true;
                }
            }
        }
        offset += total_length;
    }

    match_police_equipment_maker(metadata, &result);
    *advertisement = result;
    amulet_ble_clear_advertisement(&result);
    return true;
}

amulet_ble_action_t amulet_ble_primary_action(
    const amulet_ble_advertisement_t *advertisement
)
{
    if (advertisement == NULL) {
        return AMULET_BLE_ACTION_NONE;
    }
    if (advertisement->has_test_tracker) {
        return AMULET_BLE_ACTION_TEST_TRACKER;
    }
    if (advertisement->has_peer_amulet) {
        return AMULET_BLE_ACTION_PEER_AMULET;
    }
    if (advertisement->ambient_marker_count > 0) {
        return AMULET_BLE_ACTION_AMBIENT;
    }
    return AMULET_BLE_ACTION_NONE;
}
