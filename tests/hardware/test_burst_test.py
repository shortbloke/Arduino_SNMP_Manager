import unittest

from burst_test import canonical, packet


class BurstEncodingTests(unittest.TestCase):
    def test_equivalent_definite_lengths(self):
        self.assertEqual(canonical(b"\x04\x01x"), canonical(b"\x04\x82\x00\x01x"))

    def test_reject_truncated_value(self):
        with self.assertRaises(ValueError):
            canonical(b"\x04\x02x")

    def test_reject_indefinite_length(self):
        with self.assertRaises(ValueError):
            canonical(b"\x30\x80\x00\x00")

    def test_notification_envelope(self):
        for inform, tag in ((False, 0xA7), (True, 0xA6)):
            envelope = canonical(packet(500, 256, inform))[0]
            self.assertEqual(envelope[0], 0x30)
            version, community, pdu = envelope[1]
            self.assertEqual(version, (2, b"\x01"))
            self.assertEqual(community, (4, b"burst-test"))
            self.assertEqual(pdu[0], tag)
            identifier, error, index, bindings = pdu[1]
            self.assertEqual(identifier, (2, b"\x01\xf4"))
            self.assertEqual(error, (2, b"\x00"))
            self.assertEqual(index, (2, b"\x00"))
            self.assertEqual(len(bindings[1]), 3)
            self.assertEqual(bindings[1][2][1][1], (4, b"x" * 256))

    def test_ack_requires_matching_id_and_payload(self):
        ack = canonical(packet(1, 32, True, True))
        self.assertNotEqual(ack, canonical(packet(2, 32, True, True)))
        self.assertNotEqual(ack, canonical(packet(1, 256, True, True)))
        self.assertNotEqual(ack, canonical(packet(1, 32, True)))


if __name__ == "__main__":
    unittest.main()
