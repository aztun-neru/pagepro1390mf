import importlib.machinery
import importlib.util
import pathlib
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
loader = importlib.machinery.SourceFileLoader("pagepro1390_backend", str(HERE / "pagepro1390"))
spec = importlib.util.spec_from_loader(loader.name, loader)
backend = importlib.util.module_from_spec(spec)
loader.exec_module(backend)


class SplitStreamTests(unittest.TestCase):
    def test_reference_streams(self):
        expected = {
            "ref-blank.prn": [160, 96, 160, 98],
            "ref-square.prn": [160, 96, 192, 98],
            "ref-ps.prn": [160, 96, 4448, 98],
        }
        for name, lengths in expected.items():
            with self.subTest(name=name):
                data = (ROOT / name).read_bytes()
                chunks = backend.split_stream(data)
                self.assertEqual([len(chunk) for chunk in chunks], lengths)
                self.assertEqual(b"".join(chunks), data)

    def test_cups_stream(self):
        data = (HERE / "cups-ps.bin").read_bytes()
        chunks = backend.split_stream(data)
        self.assertEqual([len(chunk) for chunk in chunks], [160, 96, 4512, 98])
        self.assertEqual(b"".join(chunks), data)

    def test_two_pages(self):
        data = (HERE / "cups-ps.bin").read_bytes()
        two_pages = data[:-98] + data[160:]
        chunks = backend.split_stream(two_pages)
        self.assertEqual([len(chunk) for chunk in chunks], [160, 96, 4512, 96, 4512, 98])
        self.assertEqual(b"".join(chunks), two_pages)

    def test_rejects_truncated_stream(self):
        data = (HERE / "cups-ps.bin").read_bytes()
        with self.assertRaises(ValueError):
            backend.split_stream(data[:-1])


if __name__ == "__main__":
    unittest.main()
