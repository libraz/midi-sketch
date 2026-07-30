"""Keep cross-platform generation away from implementation-defined RNG helpers."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = re.compile(r"\bstd::(?:uniform_[a-z_]*distribution|shuffle)\s*(?:<|\()")


class DeterministicRngSourceTest(unittest.TestCase):
    def test_core_sources_use_rng_util_instead_of_standard_distributions(self) -> None:
        violations: list[str] = []
        for path in (ROOT / "src").rglob("*.[ch]*"):
            if path.suffix not in {".cpp", ".h"}:
                continue
            for line_number, line in enumerate(path.read_text().splitlines(), start=1):
                code = line.split("//", maxsplit=1)[0]
                if FORBIDDEN.search(code):
                    violations.append(f"{path.relative_to(ROOT)}:{line_number}: {line.strip()}")

        self.assertEqual(
            violations,
            [],
            "Use core/rng_util.h so seeded generation stays bit-identical across platforms:\n"
            + "\n".join(violations),
        )


if __name__ == "__main__":
    unittest.main()
