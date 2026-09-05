# Protocol references for the native suite

These references explain the expectations behind the SNMPv1 and community-based SNMPv2c GetRequest/response tests in `cases/`. They distinguish protocol requirements from library conventions. The suite covers selected behavior; passing it is not a conformance certification.

## Message versions

SNMPv1 uses an ASN.1 sequence containing version INTEGER 0, community OCTET STRING, and a PDU. Failed parsing and unsupported versions must cause rejection. Missing objects produce a PDU-level `noSuchName` error with a one-based error index. [RFC 1157 §§4–4.1.2](https://www.rfc-editor.org/rfc/rfc1157.html#section-4)

SNMPv2c retains the community wrapper but uses version INTEGER 1. The project's term “v2” means v2c in this context. The parser's internal `version + 1` representation must not be confused with the wire value. [RFC 1901 §3](https://www.rfc-editor.org/rfc/rfc1901.html#section-3)

## Requests and responses

Get requests address exact object instances; NULL is an appropriate placeholder. Responses preserve request IDs for correlation. Nonzero error-status means binding values must be ignored. In v2c, `noSuchObject` and `noSuchInstance` are per-binding exceptions; `endOfMibView` belongs to GetNext/GetBulk traversal. A `tooBig` response has an empty binding list. [RFC 3416 §§4.1–4.2.4](https://www.rfc-editor.org/rfc/rfc3416.html#section-4.1)

Short messages and empty binding lists are not inherently malformed. Tests cover structural validation, mixed successful/exception bindings, PDU-level errors without updates, request correlation, and short `tooBig` responses.

## Transport and BER

UDP 161 is the suggested command-responder port; 162 is for notification receivers. The UDP mapping requires reception through 484 bytes and recommends 1472. SNMP requires definite lengths and primitive simple values, but explicitly permits extra length octets. [RFC 3417 §§3, 8](https://www.rfc-editor.org/rfc/rfc3417.html#section-3)

Responses return to the originating transport endpoint. Binding the shared request/response socket to 162 can work, but 162 is not a mandatory response destination. The existing lifecycle assertion documents the library's choice. [RFC 3412 §4.1.2](https://www.rfc-editor.org/rfc/rfc3412.html#section-4.1.2)

Keep exact-byte assertions for the chosen encoder output, but do not require decoders to accept only that representation. Tests cover nonminimal definite lengths and reject indefinite lengths.

BER INTEGER contents use minimal two's-complement, most-significant octet first: 128 is `02 02 00 80`, -1 is `02 01 ff`. Unsigned application integers still require a leading zero when their high bit is set. Counter64 maximum therefore needs nine content octets; fixed eight-byte output is incorrect. OIDs combine the first two arcs as `40*first + second`, then encode subidentifiers in minimal base-128 groups. [ITU-T X.690 §§8.3, 8.19](https://www.itu.int/rec/T-REC-X.690-202102-I/en)

Exact-byte integer assertions describe minimal signed BER, not a requirement for SNMP to use CER/DER.

## Data types versus library conventions

Integer32 is signed 32-bit. OCTET STRING holds binary or textual bytes, with no implicit C terminator. OID limits are 128 subidentifiers, each at most 2^32−1—not 128 printable characters. Counter32/64 wrap; Gauge32 can increase or decrease; TimeTicks measures hundredths of a second modulo 2^32. [RFC 2578 §7.1](https://www.rfc-editor.org/rfc/rfc2578.html#section-7.1)

Tests cover embedded-zero strings and maximum subidentifiers; full protocol OID-size boundary coverage remains outstanding. The float handler's division by ten is a library convention whose suitability depends on the MIB; SNMP does not mandate that scale. C-string termination is also an API responsibility. Tests for these API conventions are not standalone protocol-conformance assertions.
