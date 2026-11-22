"""The player data's root folder (tools/gow3_data.py, GOW3_DATA_DIR): analysis/ in a
checkout or extracted package, the variable when set. Inside the AppImage the package
is read-only, so its AppRun exports GOW3_DATA_DIR and the launcher, the probe and the
starters follow it (task 16)."""
from paths import ROOT
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("gow3_data", ROOT.parent / "tools/gow3_data.py")
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)


class DataRootTests(unittest.TestCase):
    def setUp(self):
        self.saved = os.environ.get("GOW3_DATA_DIR")
        os.environ.pop("GOW3_DATA_DIR", None)
        self.addCleanup(self.restore)
        self.top = Path(tempfile.mkdtemp(prefix="tlou-top-"))

    def restore(self):
        if self.saved is None:
            os.environ.pop("GOW3_DATA_DIR", None)
        else:
            os.environ["GOW3_DATA_DIR"] = self.saved

    def test_checkout_default_is_analysis(self):
        self.assertEqual(data.data_root(self.top), self.top / "analysis")

    def test_variable_wins_and_expands(self):
        os.environ["GOW3_DATA_DIR"] = str(self.top / "custom")
        self.assertEqual(data.data_root(self.top), self.top / "custom")
        os.environ["GOW3_DATA_DIR"] = "~"
        self.assertEqual(data.data_root(self.top), Path.home())

    def test_launcher_and_probe_agree(self):
        # The launcher's paths and probe_tlou's out/user are built from the same root.
        import importlib.machinery
        import importlib.util as iu

        def load(name, path):
            loader = importlib.machinery.SourceFileLoader(name, str(path))
            module = iu.module_from_spec(iu.spec_from_loader(name, loader))
            loader.exec_module(module)
            return module

        # The launcher computes its paths at import time, so the variable must be set before
        # loading it (as AppRun sets it before starting anything).
        os.environ["GOW3_DATA_DIR"] = str(self.top / "shared")
        launcher = load("tlou_launcher_test", ROOT.parent / "launcher/tlou_launcher.pyw")
        probe = load("probe_tlou_test", ROOT.parent / "tools/probe_tlou.py")
        self.assertEqual(launcher.USER, self.top / "shared" / "tlou-user")
        self.assertEqual(launcher.INI, self.top / "shared" / "tlou-user" / "tlou.ini")
        self.assertEqual(launcher.SWITCHES, self.top / "shared" / "tlou-user" / "launcher.json")
        self.assertEqual(launcher.LOG, self.top / "shared" / "tlou" / "boot.log")
        self.assertEqual(launcher.RUN_LOG, self.top / "shared" / "tlou" / "launcher-run.log")
        self.assertEqual(launcher.GAME_RUN_LOG, self.top / "shared" / "tlou" / "game-run.log")
        # probe_tlou computes its folders inside main(); check it resolves the same root:
        self.assertEqual(probe.gow3_data.data_root(ROOT.parent), self.top / "shared")


if __name__ == "__main__":
    unittest.main()
