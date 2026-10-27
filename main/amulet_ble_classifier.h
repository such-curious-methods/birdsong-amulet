#ifndef AMULET_BLE_CLASSIFIER_H
#define AMULET_BLE_CLASSIFIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AMULET_PEER_SERVICE_UUID_LENGTH 16U
#define AMULET_PEER_SERVICE_UUID_BYTES \
    0x34, 0x36, 0x11, 0xaf, 0x30, 0x32, 0x05, 0xa9, \
    0x44, 0x43, 0xae, 0xc8, 0x50, 0xba, 0xdb, 0x33
#define AMULET_IBEACON_IDENTITY_LENGTH 20U
#define AMULET_ALTBEACON_BEACON_ID_LENGTH 20U
#define AMULET_ALTBEACON_IDENTITY_LENGTH 22U
#define AMULET_BLE_AMBIENT_IDENTITY_MAX_LENGTH \
    AMULET_ALTBEACON_IDENTITY_LENGTH
#define AMULET_BLE_MAX_AMBIENT_MARKERS 4U

typedef enum {
    AMULET_BLE_RULE_NONE,
    AMULET_BLE_RULE_TEST_TRACKER,
    AMULET_BLE_RULE_PEER_AMULET,
    AMULET_BLE_RULE_IBEACON,
    AMULET_BLE_RULE_ALTBEACON,
} amulet_ble_rule_t;

typedef enum {
    AMULET_BLE_CATEGORY_NONE,
    AMULET_BLE_CATEGORY_WORKSHOP_FIXTURE,
    AMULET_BLE_CATEGORY_LOCAL_PROTOCOL,
    AMULET_BLE_CATEGORY_LOCATION_INFRASTRUCTURE,
    AMULET_BLE_CATEGORY_LINKABILITY_INDICATOR,
    AMULET_BLE_CATEGORY_PRIVACY_PRESERVING,
    AMULET_BLE_CATEGORY_ECOSYSTEM_DISCOVERY,
    AMULET_BLE_CATEGORY_CAPABILITY_EXPOSURE,
    AMULET_BLE_CATEGORY_PERSONAL_TRACKER,
} amulet_ble_category_t;

typedef enum {
    AMULET_BLE_CONFIDENCE_NONE,
    AMULET_BLE_CONFIDENCE_STRUCTURAL,
    AMULET_BLE_CONFIDENCE_EXACT,
} amulet_ble_confidence_t;

typedef enum {
    AMULET_BLE_AMBIENT_EVIDENCE_NONE,
    AMULET_BLE_AMBIENT_EVIDENCE_IBEACON_FORMAT,
    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_STRICT_V1,
    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_PAYLOAD_MATCH,
    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_ENVELOPE_CONFLICT,
    AMULET_BLE_AMBIENT_EVIDENCE_ALTBEACON_SHAPED,
} amulet_ble_ambient_evidence_t;

// This is protocol-recognition metadata, not the later per-device observation.
// It deliberately contains no borrowed packet data or identifying values.
typedef struct {
    amulet_ble_rule_t rule;
    amulet_ble_category_t category;
    amulet_ble_confidence_t confidence;
    amulet_ble_ambient_evidence_t ambient_evidence;
} amulet_ble_classification_t;

// This transient extraction is copied into the ambient ingress queue and
// replaced by a boot-scoped digest in the application task.
typedef struct {
    uint8_t identity[AMULET_IBEACON_IDENTITY_LENGTH];
    int8_t calibration_power;
} amulet_ble_ibeacon_t;

typedef enum {
    AMULET_BLE_PDU_TYPE_UNAVAILABLE,
    AMULET_BLE_PDU_TYPE_ADV_NONCONN_IND,
    AMULET_BLE_PDU_TYPE_OTHER,
} amulet_ble_pdu_type_t;

// Scavenger-hunt beacon manufacturer data, shared by the beacon role and the
// parser: test company ID 0xFFFF, type 0xA3, version 1, beacon number 1..8.
#define AMULET_HUNT_BEACON_TYPE 0xa3U
#define AMULET_HUNT_BEACON_VERSION 0x01U
#define AMULET_HUNT_BEACON_MAX_NUMBER 8U
#define AMULET_HUNT_BEACON_DATA_LENGTH 5U

// Only the manufacturer prefix (OUI) of a public address, most significant
// byte first; never the full address. A random address's first bytes are
// random, so the prefix is meaningful only when public_address is true.
#define AMULET_BLE_ADDRESS_PREFIX_LENGTH 3U

typedef struct {
    amulet_ble_pdu_type_t pdu_type;
    bool public_address;
    uint8_t address_prefix[AMULET_BLE_ADDRESS_PREFIX_LENGTH];
} amulet_ble_scan_metadata_t;

typedef enum {
    AMULET_BLE_ALTBEACON_EVIDENCE_NONE,
    AMULET_BLE_ALTBEACON_STRICT_V1,
    AMULET_BLE_ALTBEACON_PAYLOAD_MATCH,
    AMULET_BLE_ALTBEACON_ENVELOPE_CONFLICT,
    AMULET_BLE_ALTBEACON_SHAPED,
} amulet_ble_altbeacon_evidence_t;

typedef struct {
    uint8_t beacon_id[AMULET_ALTBEACON_BEACON_ID_LENGTH];
    uint16_t company_id;
    int8_t reference_rssi;
    uint8_t reserved;
    amulet_ble_altbeacon_evidence_t evidence;
} amulet_ble_altbeacon_t;

typedef enum {
    AMULET_BLE_AMBIENT_OBSERVATION_NONE,
    AMULET_BLE_AMBIENT_OBSERVATION_LOGICAL_MARKER,
    AMULET_BLE_AMBIENT_OBSERVATION_DIAGNOSTIC,
} amulet_ble_ambient_observation_kind_t;

typedef enum {
    AMULET_BLE_CANDIDATE_MULTIPLICITY_NONE,
    AMULET_BLE_CANDIDATE_MULTIPLICITY_ONE,
    AMULET_BLE_CANDIDATE_MULTIPLICITY_REPEATED_IDENTICAL,
    AMULET_BLE_CANDIDATE_MULTIPLICITY_MULTIPLE_DISTINCT,
    AMULET_BLE_CANDIDATE_MULTIPLICITY_OVERFLOW,
} amulet_ble_candidate_multiplicity_t;

// Identity bytes are transient. The ambient task replaces them with a
// boot-scoped digest before recording an observation.
typedef struct {
    uint8_t identity[AMULET_BLE_AMBIENT_IDENTITY_MAX_LENGTH];
    uint8_t identity_length;
    amulet_ble_rule_t rule;
    amulet_ble_category_t category;
    amulet_ble_confidence_t confidence;
    amulet_ble_ambient_observation_kind_t observation_kind;
    amulet_ble_ambient_evidence_t evidence;
    amulet_ble_candidate_multiplicity_t multiplicity;
    int8_t calibration_power;
    bool calibration_power_valid;
} amulet_ble_ambient_marker_t;

// Provisional signature signals: format-level matches taken from published
// detectors (see tracker-signatures.org) and not yet confirmed with captures.
// Like the classification result, they carry no identity, address or payload.
typedef enum {
    AMULET_BLE_SIGNAL_TRACKER_SEPARATED = 1U << 0,
    AMULET_BLE_SIGNAL_SMART_GLASSES = 1U << 1,
    // An ASTM F3411 (Open Drone ID) legacy Bluetooth broadcast.
    AMULET_BLE_SIGNAL_DRONE_REMOTE_ID = 1U << 2,
    // Vendor tier: a public address under a manufacturer prefix registered to
    // a police-equipment maker. Weaker evidence than a structural match.
    AMULET_BLE_SIGNAL_POLICE_EQUIPMENT_MAKER = 1U << 3,
} amulet_ble_signal_t;

// Which tracker formats raised AMULET_BLE_SIGNAL_TRACKER_SEPARATED. Tile has
// no separated indicator, so a Tile frame raises the signal on its own.
typedef enum {
    AMULET_BLE_TRACKER_FAMILY_FIND_MY = 1U << 0,
    AMULET_BLE_TRACKER_FAMILY_SMARTTAG = 1U << 1,
    AMULET_BLE_TRACKER_FAMILY_FIND_HUB = 1U << 2,
    AMULET_BLE_TRACKER_FAMILY_DULT = 1U << 3,
    AMULET_BLE_TRACKER_FAMILY_TILE = 1U << 4,
} amulet_ble_tracker_family_t;

// This is a synchronous, transient view of one complete advertisement. It must
// be cleared after any extracted identity is copied into the ambient ingress pool.
typedef struct {
    amulet_ble_ambient_marker_t ambient_markers[AMULET_BLE_MAX_AMBIENT_MARKERS];
    uint8_t ambient_marker_count;
    bool has_test_tracker;
    bool has_peer_amulet;
    bool ambient_marker_overflow;
    uint8_t signals;
    uint8_t tracker_families;
    // Scavenger-hunt beacon number, or 0 when none was present.
    uint8_t hunt_beacon;
} amulet_ble_advertisement_t;

typedef enum {
    AMULET_BLE_ACTION_NONE,
    AMULET_BLE_ACTION_TEST_TRACKER,
    AMULET_BLE_ACTION_PEER_AMULET,
    AMULET_BLE_ACTION_AMBIENT,
} amulet_ble_action_t;

amulet_ble_classification_t amulet_ble_classify_manufacturer_data(
    const uint8_t *data,
    size_t length
);

amulet_ble_classification_t amulet_ble_classify_service_uuid128(
    const uint8_t *uuid,
    size_t length
);

bool amulet_ble_parse_ibeacon(
    const uint8_t *data,
    size_t length,
    amulet_ble_ibeacon_t *ibeacon
);

bool amulet_ble_parse_altbeacon(
    const uint8_t *data,
    size_t length,
    amulet_ble_pdu_type_t pdu_type,
    amulet_ble_altbeacon_t *altbeacon
);

// Accepts only the exact hunt-beacon manufacturer data and returns its number.
bool amulet_ble_parse_hunt_beacon(
    const uint8_t *data,
    size_t length,
    uint8_t *beacon
);

bool amulet_ble_altbeacon_evidence_is_dispatchable(
    amulet_ble_altbeacon_evidence_t evidence
);

bool amulet_ble_classify_advertisement(
    const uint8_t *data,
    size_t length,
    const amulet_ble_scan_metadata_t *metadata,
    amulet_ble_advertisement_t *advertisement
);

void amulet_ble_clear_advertisement(amulet_ble_advertisement_t *advertisement);

amulet_ble_action_t amulet_ble_primary_action(
    const amulet_ble_advertisement_t *advertisement
);

#endif
