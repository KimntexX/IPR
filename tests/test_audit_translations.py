from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from audit_translations import inventory,record,source_hash


class TranslationAuditTests(unittest.TestCase):
    def test_missing_unverified_and_source_drift(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'guide.md';source.write_text('Texte français',encoding='utf-8')
            self.assertEqual(record(root,Path('guide.md'))['status'],'missing')
            english=root/'guide_en.md';english.write_text('English text',encoding='utf-8')
            self.assertEqual(record(root,Path('guide.md'))['status'],'present_unverified')
            digest=source_hash(source)
            english.write_text(f'<!-- translation-source: guide.md; sha256: {digest} -->\nEnglish text',encoding='utf-8')
            self.assertEqual(record(root,Path('guide.md'))['status'],'source_hash_matches')
            source.write_text('Texte modifié',encoding='utf-8')
            self.assertEqual(record(root,Path('guide.md'))['status'],'source_changed')

    def test_wrong_source_and_exclusions(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'guide.tex').write_text('Cours',encoding='utf-8')
            (root/'guide_en.tex').write_text('% translation-source: other.tex; sha256: '+'0'*64,encoding='utf-8')
            self.assertEqual(record(root,Path('guide.tex'))['status'],'wrong_source')
            result=inventory(root,[Path('guide.tex'),Path('guide_en.tex'),Path('AGENTS.md'),Path('build/source.tex')])
            self.assertEqual(len(result['records']),1)

    def test_line_endings_do_not_invent_source_drift(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);a=root/'a.md';b=root/'b.md'
            a.write_bytes(b'Title\r\nText\r\n');b.write_bytes(b'Title\nText\n')
            self.assertEqual(source_hash(a),source_hash(b))


if __name__=='__main__':unittest.main()
