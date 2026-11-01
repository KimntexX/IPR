import importlib.util
from pathlib import Path
import json
import unittest

SPEC = importlib.util.spec_from_file_location('bench_publication', Path(__file__).resolve().parents[1]/'scripts/audit_raw_bench_publication.py')
audit = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(audit)


class PublicationAuditTests(unittest.TestCase):
    def test_no_credential_values_in_findings(self):
        token = ('ghp_'+'A'*40).encode('ascii')
        findings = audit.inspect_content(token, 'example.txt')
        self.assertEqual(findings[0]['kind'], 'credential_pattern')
        self.assertNotIn(token.decode(), json.dumps(findings))

    def test_sensitive_json_field(self):
        findings = audit.inspect_content(b'{"metadata":{"GPSLatitude":[12,3,4]}}', 'data.json')
        self.assertEqual(findings[0]['key_path'], '$.metadata.GPSLatitude')
        self.assertNotIn('[12, 3, 4]', json.dumps(findings))

    def test_null_unknown_is_not_private_data(self):
        self.assertEqual(audit.inspect_content(b'{"GPSLatitude":null,"serialNumber":""}', 'data.json'), [])

    def test_profile_and_capture_binaries_blocked(self):
        for suffix in ('.dcp','.lcp','.icc','.bin','.CR3','.tiff','.exe'):
            self.assertEqual(audit.inspect_content(b'fixture', 'file'+suffix)[0]['kind'], 'private_or_generated_file_type')

    def test_invalid_json_and_nonfinite_blocked(self):
        for data in (b'{bad}', b'{"measurement":NaN}'):
            self.assertEqual(audit.inspect_content(data, 'data.json')[0]['kind'], 'invalid_json')

    def test_provenance_without_pixels_allowed(self):
        data = json.dumps(dict(source='C:/Users/victo/Pictures/image.CR3', sha256='a'*64,
                               roi=[0,0,64,64], sigma=.0003)).encode()
        self.assertEqual(audit.inspect_content(data, 'measurement.json'), [])


if __name__ == '__main__':
    unittest.main()
