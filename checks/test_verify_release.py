import hashlib
import io
import unittest
import zipfile
from verify_release import verify


class VerifyTests(unittest.TestCase):
    def test_rejects_tampered_download(self):
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            verify(b'bad', {'name': 'test', 'bytes': 3, 'sha256': '0' * 64})

    def test_rejects_incomplete_download(self):
        with self.assertRaisesRegex(ValueError, 'Size'):
            verify(b'', {'name': 'test', 'bytes': 3})

    def test_rejects_unexpected_windows_payload(self):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr('unexpected.txt', 'test')
        data = buffer.getvalue()
        with self.assertRaisesRegex(ValueError, 'Unexpected Windows'):
            verify(data, {'name': 'test.zip', 'bytes': len(data),
                          'sha256': hashlib.sha256(data).hexdigest(), 'kind': 'windows'})


if __name__ == '__main__':
    unittest.main()
