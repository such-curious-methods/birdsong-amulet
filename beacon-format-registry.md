# BLE Location Beacon Format Registry

Last reviewed: 2026-08-31

This registry records Bluetooth Low Energy location-beacon formats considered
for Amulet. It is both a research index and an implementation contract: each
entry identifies its sources, exact structural discriminator, identity and
rotation semantics, safe user-facing claim, and current support status.

A format match describes bytes observed on the air. It does not authenticate the
transmitter, establish who manufactured or deployed it, prove that a phone
received it, or reveal why it is present. This registry does not cover passive
retail receivers, which may listen without transmitting anything detectable.

## Evidence Policy

The registry uses these provenance labels:

| Label | Evidence |
| --- | --- |
| `N` | Bluetooth Core, Core Specification Supplement, or Assigned Numbers; establishes the BLE envelope or identifier assignment, not a proprietary payload meaning |
| `P1` | Public first-party normative payload specification with a recorded version or immutable revision |
| `P2` | First-party API, SDK, deployment guide, source, or packet example without a public normative byte specification |
| `P3` | Independently implemented or reverse-engineered layout corroborated by multiple implementations or controlled captures |
| `P4` | Single community implementation, blog, wiki, or uncorroborated capture; experimental only |

Every production matcher needs exact positive, truncated, overlong, malformed,
and near-match fixtures. Company identifiers and service UUIDs are identifiers,
not signatures. A company assignment must never be used as the sole format
classifier.

The BLE envelope references are:

- `N`: [Bluetooth Core 6.3 GAP advertising-data
  structure](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core_v6.3/out/en/host/generic-access-profile.html#UUID-c2a0b759-8ef4-7055-c13b-17c083691361)
- `N`: [Core Specification Supplement v15 data
  types](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CSS_v15/out/en/core-supplementary-features/data-types-specification.html#UUID-b1d0edbc-fc9e-507a-efe4-3fd4b4817a52)
- `N`: [Bluetooth SIG Assigned
  Numbers](https://www.bluetooth.com/specifications/assigned-numbers/), including
  the public [machine-readable YAML
  repository](https://bitbucket.org/bluetooth-SIG/public/src/main/assigned_numbers/)
- `N`: [Pinned Assigned Numbers repository
  revision](https://bitbucket.org/bluetooth-SIG/public/commits/90e83ed1e959291dc903e604d3ec8c3d4ebc8a34)
  and its [company-identifier
  snapshot](https://bitbucket.org/bluetooth-SIG/public/src/90e83ed1e959291dc903e604d3ec8c3d4ebc8a34/assigned_numbers/company_identifiers/company_identifiers.yaml)
  for reproducible AltBeacon research

Assigned Numbers can establish that `0xFF` is Manufacturer Specific Data, that
`0x16` is Service Data with a 16-bit UUID, and who received a company or service
identifier assignment. It does not define the remaining proprietary bytes or
authenticate the broadcaster.

## Registry Summary

| Format | Primary discriminator | Advertised identity | Rotation | Amulet status | Main source |
| --- | --- | --- | --- | --- | --- |
| iBeacon-format | Amulet strict profile: manufacturer data `4c 00 02 15`, exactly 25 bytes including company ID | UUID + major + minor | No format-defined rotation | Implemented and OTA verified | [Apple resources](https://developer.apple.com/ibeacon/) plus [corroborated parser](https://github.com/sandeepmistry/node-bleacon/blob/f8a60ae7f836af852eb92dbb7dff7d16a661e22b/lib/bleacon.js#L70-L87) |
| AltBeacon v1.0 | Manufacturer data with a SIG-assigned company ID followed by `be ac`, exactly 26 bytes including company ID | 20-byte Beacon ID; Amulet namespaces its key by company ID | No format-defined rotation | Implemented and OTA verified | [AltBeacon v1.0 specification](https://github.com/AltBeacon/spec/blob/d5c7f6da1192313d28222f9da2e0251074153252/README.md) |
| Eddystone-UID | Service UUID `0xFEAA`, frame `0x00` | 10-byte namespace + 6-byte instance | No format-defined rotation | Researched, not implemented | [Eddystone-UID specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-uid/README.md) |
| Eddystone-URL | Service UUID `0xFEAA`, frame `0x10` | Encoded URL content, not a device identity | No format-defined rotation | Researched, not implemented | [Eddystone-URL specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-url/README.md) |
| Eddystone-TLM v0 | Service UUID `0xFEAA`, frame `0x20`, version `0x00` | None | Not applicable | Researched, not implemented | [Plain TLM specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-tlm/tlm-plain.md) |
| Eddystone-TLM v1 | Service UUID `0xFEAA`, frame `0x20`, version `0x01` | None; address/time can select an EID candidate before resolver processing | No identity semantics; ciphertext and salt may vary | Researched, not implemented | [Encrypted TLM specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-tlm/tlm-encrypted.md) |
| Eddystone-EID | Service UUID `0xFEAA`, frame `0x30` | 8-byte ephemeral identifier field | A conforming provisioned beacon rotates it; one frame cannot prove rotation | Researched, not implemented | [Eddystone-EID specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-eid/README.md) |

## iBeacon-Format

**Status:** Implemented as `AMULET_BLE_RULE_IBEACON` and verified with host and
over-the-air fixtures.

**Evidence:** Apple publishes the identity, ranging, and deployment semantics,
but its formal Proximity Beacon Specification is available only after accepting
the license on the [iBeacon developer page](https://developer.apple.com/ibeacon/).
The public byte layout below is therefore recorded as `P3`, corroborated by the
pinned [node-bleacon
parser](https://github.com/sandeepmistry/node-bleacon/blob/f8a60ae7f836af852eb92dbb7dff7d16a661e22b/lib/bleacon.js#L70-L87)
and the Android Beacon Library's [layout
test](https://github.com/AltBeacon/android-beacon-library/blob/aca69f9bc7f3526032e6e5fcc3593a3259c7d2d4/lib/src/test/java/org/altbeacon/beacon/service/scanner/ScanFilterUtilsTest.java#L100-L116).
Apple's public [Getting Started with iBeacon
guide](https://developer.apple.com/ibeacon/Getting-Started-with-iBeacon.pdf) and
[Core Location proximity
documentation](https://developer.apple.com/documentation/corelocation/determining-the-proximity-to-an-ibeacon-device)
provide `P2` semantics.

The public implementations agree on the first 25 manufacturer-data bytes and
their field meanings. They do not establish exact-length conformance:
node-bleacon accepts trailing bytes, and the cited Android test does not assert a
total length. Requiring exactly 25 bytes is Amulet's conservative strict-profile
policy for rejecting ambiguous extensions, not a claim about Apple's
license-gated normative specification.

### Wire Layout

```text
1a ff 4c 00 02 15
[proximity UUID: 16 bytes]
[major: 2 bytes, big-endian]
[minor: 2 bytes, big-endian]
[measured power: 1 signed byte]
```

| Manufacturer-data offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | Company Identifier | Exactly `4c 00`, the little-endian wire form of `0x004C` |
| 2 | 1 | Type | Exactly `0x02` |
| 3 | 1 | Remaining body length | Exactly `0x15` |
| 4 | 16 | Proximity UUID | Preserve transmitted byte order |
| 20 | 2 | Major | Unsigned big-endian value |
| 22 | 2 | Minor | Unsigned big-endian value |
| 24 | 1 | Measured power | Signed two's-complement RSSI reference measured at one metre |

The canonical structure used by Amulet is 27 bytes: length `0x1A`, AD type
`0xFF`, and 25 manufacturer-data bytes. A preceding Flags structure is common
but is not part of this format and must not be required.

### Amulet Strict Match

- Parse within one complete AD structure; never scan arbitrary bytes for the
  prefix.
- Require AD type `0xFF` and exactly 25 manufacturer-data bytes.
- Require the complete `4c 00 02 15` prefix.
- Accept all UUID, major, minor, and signed calibration byte values structurally.
- Reject truncation, extension, split fields, and prefix or inner-length changes
  as a conservative project policy.

### Identity And Claim

The logical advertised identity is UUID + major + minor. Measured power,
observed RSSI, and BLE address are not identity fields. Different physical
beacons can intentionally share one tuple, so this is a deployment identity, not
a hardware serial number.

Safe claim:

> A well-formed Manufacturer Specific Data element matching the de facto
> iBeacon-format layout is broadcasting nearby.

Do not claim Apple manufacture, licensing, certification, authenticity, precise
distance, shopper tracking, or phone-app reporting. The format has no packet
authentication. Continue using `iBeacon-format` as descriptive wording and do
not use Apple's certification badge or imply endorsement.

## AltBeacon v1.0

**Status:** Implemented as `AMULET_BLE_RULE_ALTBEACON`. Exact, near-match,
evidence, full-advertisement, multiplicity, and overflow fixtures pass under the
host and sanitizer builds. ESP-IDF integration builds successfully. A strict
BlueZ fixture is verified over the air, including repeat suppression and
boot-scoped identifier rotation.

**Sources:** `P1` [immutable AltBeacon Protocol Specification
v1.0](https://github.com/AltBeacon/spec/blob/d5c7f6da1192313d28222f9da2e0251074153252/README.md),
the publisher's [open-specification and licensing
statement](https://altbeacon.org/), and the `P2` Android Beacon Library
[parser implementation](https://github.com/AltBeacon/android-beacon-library/blob/aca69f9bc7f3526032e6e5fcc3593a3259c7d2d4/lib/src/main/java/org/altbeacon/beacon/BeaconParser.java).

The publisher describes AltBeacon as a proposal that is free to implement
without royalties and labels the published work CC BY-SA 4.0. That licensing
statement is a mutable website source retrieved on 2026-08-30; the pinned v1.0
specification repository itself has no license file. AltBeacon is not a
Bluetooth SIG-adopted beacon standard.

### Wire Layout

```text
1b ff [company ID: 2 bytes, little-endian] be ac
[Beacon ID: 20 bytes]
[reference RSSI: 1 signed byte]
[manufacturer reserved: 1 byte]
```

| Manufacturer-data offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | Company Identifier | Decode little-endian; strict v1.0 conformance requires a SIG-assigned value, but AltBeacon has no single fixed company ID |
| 2 | 2 | Beacon code | Exactly `be ac`, big-endian |
| 4 | 20 | Beacon ID | One contiguous big-endian transmitted identifier |
| 24 | 1 | Reference RSSI | Signed one-metre reference in the specified range `-127..0` |
| 25 | 1 | Manufacturer reserved | Any byte; meaning depends on company ID |

The complete Manufacturer Specific AD structure is 28 bytes: length `0x1B`, AD
type `0xFF`, and 26 manufacturer-data bytes. The common Flags structure shown in
examples is not part of AltBeacon and must not be required.

### Strict Match

- Parse one AD structure with type `0xFF` and exactly 26 manufacturer-data bytes.
- Decode the company ID and, for full v1.0 conformance, verify that it appears in
  the pinned Bluetooth SIG [company-identifier
  snapshot](https://bitbucket.org/bluetooth-SIG/public/src/90e83ed1e959291dc903e604d3ec8c3d4ebc8a34/assigned_numbers/company_identifiers/company_identifiers.yaml).
  This still proves neither source nor authenticity.
- Require `be ac` immediately after the company ID.
- Require exactly 20 ID bytes, one signed reference-RSSI byte, and one reserved
  byte.
- Accept all Beacon ID and reserved values. Require reference RSSI in `-127..0`.
- If link-layer metadata is unavailable, describe the result as an AltBeacon
  payload match rather than claiming full `ADV_NONCONN_IND` transmission
  conformance.
- If company assignment is not checked or the value is absent from the pinned
  snapshot, describe the result as AltBeacon-shaped rather than strict v1.0
  conformance. Absence from an old snapshot is not proof that a value is
  unassigned.

The AltBeacon parser contract therefore has four evidence labels:

| Label | Evidence |
| --- | --- |
| `ALTBEACON_STRICT_V1` | Exact payload, company ID present in the pinned snapshot, and required link-layer metadata observed |
| `ALTBEACON_PAYLOAD_MATCH` | Exact payload and assigned company ID, but link-layer metadata unavailable |
| `ALTBEACON_ENVELOPE_CONFLICT` | Exact payload and assigned company ID, but observed link-layer metadata contradicts the required `ADV_NONCONN_IND` PDU |
| `ALTBEACON_SHAPED` | Exact byte shape, but company assignment was not checked or not found in the pinned snapshot |

### Identity And Claim

The protocol's advertised identity is the 20-byte Beacon ID. Amulet should use
`format domain + company ID + Beacon ID` as its collision-resistant local
observation key because different company namespaces may reuse the same 20
bytes. This project key does not redefine the protocol field. Exclude reference
RSSI, observed RSSI, BLE address, and the reserved byte unless documentation for
that company explicitly defines the reserved byte as identity.

Safe claim:

> The manufacturer data structurally matches the published AltBeacon v1.0
> payload.

For `ALTBEACON_SHAPED`, say only:

> The manufacturer data has the byte shape of an AltBeacon v1.0 payload; company
> assignment and full transmission conformance were not established.

For `ALTBEACON_ENVELOPE_CONFLICT`, say only:

> The manufacturer data matches the AltBeacon v1.0 payload layout, but the
> observed link-layer transmission contradicts the format's PDU requirement.

Only `ALTBEACON_STRICT_V1` and `ALTBEACON_PAYLOAD_MATCH` may create a logical
location-marker entry. Shaped or envelope-conflicting results produce only a
rate-limited diagnostic and no wearer notice. The identity-key guidance above
applies only after that evidence threshold passes.

The firmware consumes a generated 547-byte assignment bitset bundled with its
source. The regeneration script rejects input unless the pinned SIG YAML's
SHA-256 matches the recorded snapshot.
NimBLE's observed PDU type supplies link-layer evidence; raw advertisement data
longer than the 31-byte legacy limit is envelope-conflicting even when the API
does not label its PDU. Dispatchable logical candidates collapse only when all
candidate bytes are identical; diagnostic candidates may coalesce by evidence.
Multiple distinct logical candidates for one rule and candidate overflow create
no entity or wearer notice.

Do not claim that the company-ID assignee made or authorized the transmitter, or
that the Beacon ID is globally unique. AltBeacon carries no authentication.

## Eddystone Common Envelope

**Status:** Researched; the current Amulet advertisement walker validates
16-bit service-data shape but does not yet classify Eddystone.

**Sources:** `P1` [Eddystone common protocol
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/protocol-specification.md)
and [Apache 2.0
license](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/LICENSE).
Google archived the repository on 2022-12-29. The formats remain relevant to
Amulet's legacy-format research scope, but the source is no longer maintained.
That inclusion is a project decision, not evidence about current deployment
counts.

Every conforming Eddystone advertisement contains:

- A Complete List of 16-bit Service UUIDs AD structure containing `0xFEAA`.
- A Service Data - 16-bit UUID AD structure, type `0x16`, beginning with service
  UUID `0xFEAA`.
- A known frame byte whose low nibble is zero.

When `0xFEAA` is the only listed UUID, the raw fields begin:

```text
03 03 aa fe
?? 16 aa fe [frame payload P]
```

The UUID is little-endian in BLE AD data (`aa fe`). Multi-byte fields inside the
Eddystone payload `P` are big-endian. The Service Data AD length byte is
`3 + sizeof(P)`: one AD-type byte, two UUID bytes, and `P`.

A decoder whose platform API hides the Complete UUID List can establish an
Eddystone-format service-data match but cannot claim complete envelope
conformance. Amulet's raw advertisement parser should verify both structures.

### Layered Classification Contract

Payload validity, advertisement validity, envelope evidence, and candidate
multiplicity are separate outputs. Combining them into one enum would make
valid-plus-invalid and missing-versus-hidden evidence impossible to represent.

**Advertisement status:**

| Status | Meaning and required action |
| --- | --- |
| `ADVERTISEMENT_VALID` | BLE framing, known fixed-width field shapes, and AD-structure cardinality rules passed |
| `ADVERTISEMENT_NOT_OBSERVED` | A normalized platform API exposed decoded service data but not the raw advertisement; framing, hidden fields, and cardinality are unavailable, so no wire-validity claim is permitted |
| `INVALID_STRUCTURE_SET` | The AD boundaries are readable, but a standard data-type rule failed, such as duplicate Complete 16-bit UUID List structures; reject all dispatch from this advertisement |
| `MALFORMED_ADVERTISEMENT` | A zero-length or truncated field crossed the supplied boundary; reject all dispatch because later boundaries cannot be trusted |

**Per-candidate frame status:**

| Status | Meaning |
| --- | --- |
| `VALID_SUPPORTED_FRAME` | One bounded `0xFEAA` service-data element passed a detailed frame rule below |
| `UNSUPPORTED_FRAME` | The frame byte exists, its reserved low nibble is zero, and its high nibble or a version/documented legacy form is not implemented |
| `INVALID_FORMAT_CANDIDATE` | A bounded candidate has no frame byte, has a nonzero reserved low nibble, or uses a supported frame type with invalid length, reserved bytes, encoding, or fields |

**Envelope evidence for each valid candidate:**

| Evidence | Meaning |
| --- | --- |
| `REQUIRED_UUID_PRESENT` | A single valid Complete 16-bit UUID List contained `0xFEAA`; full envelope evidence is present |
| `UUID_LIST_PRESENT_MISSING_FEAA` | A valid Complete 16-bit UUID List was present but omitted `0xFEAA`; payload match only, with a protocol-envelope conflict |
| `UUID_LIST_ABSENT` | Raw advertisement data was available, but no Complete 16-bit UUID List was present; payload match only |
| `UUID_LIST_UNAVAILABLE` | The platform API exposed service data but hid the UUID list; payload match only with unavailable evidence |

**Candidate multiplicity:**

| Status | Meaning and required action |
| --- | --- |
| `NONE` | No valid supported candidate |
| `ONE` | One valid candidate; it may dispatch with its envelope-evidence label |
| `REPEATED_IDENTICAL` | Multiple byte-identical valid candidates; collapse to one report |
| `MULTIPLE_DISTINCT` | More than one distinct valid candidate; return all bounded candidates for diagnostics, but create no entity or wearer notice |
| `CANDIDATE_OVERFLOW` | Candidate capacity was exceeded; create no entity or wearer notice and increment a rate-limited diagnostic counter |
| `MULTIPLICITY_UNAVAILABLE` | A normalized platform API may have collapsed repeated elements; retain payload evidence only for diagnostics or a category aggregate, and create no entity or wearer notice |

An invalid candidate remains recorded alongside a valid candidate and does not
erase it. Invalid advertisement status overrides every candidate and suppresses
dispatch. This makes duplicate-list, valid-plus-invalid, and conflicting-list
behavior deterministic rather than "last one wins."

Advertisement-level fixtures must cover every status and evidence value,
duplicate UUID lists, duplicate service-data elements, valid-plus-invalid
candidates, multiple distinct candidates, overflow, and payload-only versus
full-envelope matches. The detailed frame rules below determine per-candidate
validity.

The detailed safe claims below assume `REQUIRED_UUID_PRESENT`. For any other
envelope evidence, use "service data matches the Eddystone-X payload layout" and
state exactly whether the required Complete UUID List was absent, unavailable,
or present without `0xFEAA`.

## Eddystone-UID

**Status:** Researched; parser and fixtures not yet implemented.

**Source:** `P1` [Eddystone-UID frame
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-uid/README.md).

### Service-Data Payload

| `P` offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Frame type | Exactly `0x00` |
| 1 | 1 | Ranging data | Signed calibrated power at zero metres, specified range `-100..+20` dBm |
| 2 | 10 | Namespace ID | Self-assigned namespace |
| 12 | 6 | Instance ID | Instance within the namespace |
| 18 | 2 | Reserved | Both bytes must be zero |

Strict current conformance requires exactly 20 payload bytes and Service Data AD
length `0x17`. The specification documents older 18-byte frames that omit the
reserved bytes; if supported, they must be reported as a separate legacy
compatibility form rather than silently accepted as current conformance.

The logical advertised identity is namespace + instance. It is self-assigned,
unauthenticated, and not tied to a manufacturer registry. Ranging data is not
identity and uses a zero-metre reference, unlike iBeacon-format and AltBeacon's
one-metre references.

Safe claim:

> A structurally valid Eddystone-UID frame is broadcasting a namespace and
> instance marker nearby.

Do not call it a Google-manufactured device or an authenticated physical device.

## Eddystone-URL

**Status:** Researched; parser and fixtures not yet implemented.

**Source:** `P1` [Eddystone-URL frame
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-url/README.md).

### Service-Data Payload

| `P` offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Frame type | Exactly `0x10` |
| 1 | 1 | Ranging data | Signed calibrated power at zero metres, specified range `-100..+20` dBm |
| 2 | 1 | URL scheme | `0x00..0x03`: HTTP/HTTPS, with or without `www.` |
| 3 | 1-17 | Encoded URL | Expansion codes `0x00..0x0D` or printable US-ASCII `0x21..0x7E` |

Require exactly 4-20 payload bytes, giving Service Data AD length `0x07..0x17`.
Reserved encoded bytes `0x0E..0x20` and `0x7F..0xFF` are malformed. A structurally
valid encoding can still decode to a nonsensical or malicious URL.

The URL is content, not a physical-device identity. Multiple beacons may
advertise one URL, and one beacon may be reconfigured. If Amulet deduplicates it,
HMAC exactly `scheme byte || encoded URL bytes` under a URL-specific boot domain.
Exclude frame type and ranging data so calibration changes do not change the
content key. Never print the raw or decoded URL by default and never visit it
automatically.

Safe claim:

> An Eddystone-URL-format content marker is broadcasting nearby.

Do not claim control or safety of the destination, physical-device uniqueness,
or that any phone opened or reported the URL.

## Eddystone-TLM v0, Plain Telemetry

**Status:** Researched; parser and fixtures not yet implemented.

**Source:** `P1` [unencrypted Eddystone-TLM frame
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-tlm/tlm-plain.md).

### Service-Data Payload

| `P` offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Frame type | Exactly `0x20` |
| 1 | 1 | Version | Exactly `0x00` |
| 2 | 2 | Battery voltage | Big-endian, 1 mV per bit; zero means unsupported |
| 4 | 2 | Beacon temperature | Big-endian signed 8.8 fixed point; `0x8000` means unsupported |
| 6 | 4 | Advertisement count | Big-endian count since power-on or reboot |
| 10 | 4 | Uptime | Big-endian count in 0.1-second units |

Require exactly 14 payload bytes and Service Data AD length `0x11`. Do not add
arbitrary battery, temperature, or counter ranges as wire-validity rules.

TLM contains no beacon ID. The specification expects it to be interleaved with
UID or URL and associated transiently by device address. Amulet must not create a
logical identity or repeat key from TLM fields. Battery, temperature, count, and
uptime trajectories must not be used to bridge address rotations or infer a
physical identity. Plain TLM has no message-integrity protection.

The initial Amulet implementation should leave TLM-to-UID/URL attribution
disabled and emit only a rate-limited category aggregate. A future candidate
association may use `address type + address bytes + bounded time window` in RAM,
but must first define and fixture-test the window against owned hardware. An
address/time match is candidate evidence, not identity proof.

Safe claim:

> A structurally valid unencrypted Eddystone telemetry frame is broadcasting
> nearby.

Values are reported by an unauthenticated broadcaster. Battery millivolts are
not battery percentage, and the beacon's sensor temperature is not necessarily
ambient temperature.

## Eddystone-TLM v1, Encrypted Telemetry

**Status:** Researched; parser, resolver, and fixtures not yet implemented.

**Source:** `P1` [encrypted Eddystone-TLM frame
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-tlm/tlm-encrypted.md).

### Service-Data Payload

| `P` offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Frame type | Exactly `0x20` |
| 1 | 1 | Version | Exactly `0x01` |
| 2 | 12 | Encrypted telemetry | AES-EAX ciphertext |
| 14 | 2 | Salt | Big-endian random salt |
| 16 | 2 | MIC | Truncated message-integrity tag |

Require exactly 18 payload bytes and Service Data AD length `0x15`. A passive
scanner without resolver registration data can validate only the outer
structure. Decryption and integrity validation require the EID identity key,
clock information, AES-EAX processing, and a successful MIC comparison.

TLM v1 has no standalone identity. Address and near-in-time observation can
select an EID candidate. A resolver then resolves that EID to obtain key and
clock material, after which decryption and a successful MIC provide limited
integrity evidence for the telemetry association. Pairing, EID resolution, and
MIC validation are three distinct steps. The MIC is only 16 bits; the registry
does not treat it as proof of freshness, replay resistance, or physical device
identity.

The initial passive field guide should not attempt this association without
resolver data. It should emit only a rate-limited encrypted-telemetry category
aggregate and must not hash changing ciphertext or salt as an entity key.

Safe unresolved claim:

> An Eddystone encrypted-telemetry-format frame was observed; its telemetry and
> association are unresolved.

Do not parse ciphertext as plain telemetry or imply that the MIC was verified.

## Eddystone-EID

**Status:** Researched; structural parser and fixtures not yet implemented. A
resolver is outside the initial passive field-guide scope.

**Sources:** `P1` [Eddystone-EID frame
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-eid/README.md)
and [EID computation and registration
specification](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-eid/eid-computation.md).

### Service-Data Payload

| `P` offset | Size | Field | Validation or meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | Frame type | Exactly `0x30` |
| 1 | 1 | Ranging data | Signed calibrated power at zero metres; accept every `int8` structurally because the frame source states no normative range |
| 2 | 8 | EID | Ephemeral identifier bytes |

Require exactly 10 payload bytes and Service Data AD length `0x0D`. Any eight EID
bytes are structurally possible. Rotation exponent, time, and key are absent from
the broadcast, so a standalone passive scanner cannot establish cryptographic
validity or resolve the beacon.

The format defines periodic EID rotation. Its privacy guidance also recommends
rotating the BLE address and not interleaving a stable device-unique frame. Amulet
must not treat an EID or current address as a permanent identity. Unresolved EIDs
should contribute to a scene-level or category aggregate rather than creating a
new wearer notice at every rotation.

Safe claim:

> A structurally valid Eddystone-EID-format ephemeral-identifier field was
> observed.

Do not call it authenticated, resolved, privacy-preserving in practice, or tied
to a particular physical device. Resolver evidence may associate it with a
registered logical resolver record, while repeated temporal observations may
show that the transmitted value or address rotated. Neither establishes one
physical piece of hardware or the transmitter's privacy behavior in practice.

## Identity, Suppression, And Correlation

One physical beacon may alternate between multiple formats. Passive packet
structure alone generally cannot prove that an iBeacon-format, AltBeacon, and
Eddystone frame came from one physical transmitter. Address-based correlation is
especially fragile when addresses rotate.

These concepts must remain distinct:

| Concept | Purpose | Allowed evidence |
| --- | --- | --- |
| Logical marker key | Update one observation for the same advertised logical content | Explicit identity fields: iBeacon tuple, AltBeacon Beacon ID under company namespace, Eddystone UID tuple, or Eddystone URL scheme + body |
| Packet repeat suppression | Prevent a burst of equivalent reports from flooding queues | Exact valid candidate bytes within an explicitly bounded, fixture-tested per-rule interval; this is not an entity identity |
| Category aggregate | Report that a class is present without inventing an entity | Rule and category only; required for TLM and unresolved EID |
| Candidate correlation | Temporarily consider whether two frames may be related | Same address type + address bytes within an explicitly bounded, fixture-tested RAM-only window |
| Resolver/MIC-validated evidence | Attribute encrypted telemetry to a resolved logical EID record with limited integrity evidence | Successful EID resolution followed by successful eTLM MIC validation; this does not establish freshness or physical identity |
| Physical-device identity | Claim that observations came from one piece of hardware | Not established by these passive packet formats alone |

Amulet should therefore:

- Use domain-separated, boot-scoped digests only for formats with explicit
  logical marker fields.
- Give TLM no entity key. Do not hash its changing counters, ciphertext, salt, or
  full payload as an identity or repeat key.
- Treat unresolved EIDs as category aggregates. Repeat suppression is disabled
  until a per-rule maximum interval is explicitly selected and fixture-tested;
  any future suppression key must not become an entity entry.
- Keep address observations separate from logical protocol identity.
- Keep candidate correlation disabled until its maximum window is explicitly
  selected and tested for that format and hardware.
- Avoid reporting a count of physical devices when only logical markers were
  observed.
- Never merge formats using calibration, RSSI, packet timing, telemetry-counter
  continuity, battery, temperature, or uptime trajectories.

## Research Queue

The next registry work should identify first-party sources for commercially
deployed vendor-specific beacon and telemetry formats. An entry will not be
added merely because a company ID or UUID appears in a scan. Candidates require
documented payload semantics or controlled captures from owned hardware, plus a
near-match corpus.

Research priority:

1. Extend the raw advertisement result to retain strict 16-bit UUID and service
   data fields, then implement Eddystone-UID.
2. Add URL, plain TLM, EID, and encrypted-TLM as distinct semantic rules rather
   than one broad `0xFEAA` match.
3. Survey target venues and hardware vendors to choose proprietary formats for
   source-backed research.
