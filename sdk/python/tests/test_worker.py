import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from llamacode_harness import FrameError, PROTOCOL, decode_frame, encode_frame


class WorkerSdkTests(unittest.TestCase):
    def test_round_trip_and_chunk_independent_header(self):
        raw = encode_frame({"protocol": PROTOCOL, "type": "hello", "nonce": "n"})
        self.assertEqual(decode_frame(io.BytesIO(raw)),
                         {"protocol": PROTOCOL, "type": "hello", "nonce": "n"})

    def test_rejects_wrong_protocol(self):
        with self.assertRaises(FrameError):
            encode_frame({"protocol": "wrong", "type": "hello"})


if __name__ == "__main__":
    unittest.main()
