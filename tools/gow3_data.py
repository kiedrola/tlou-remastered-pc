"""The player data's root folder (the GOW3_DATA_DIR environment variable).

The player data (the run logs and the boot preparation in tlou/, the game's settings
tlou.ini, the launcher's launcher.json, the saves and the shader cache in tlou-user/)
lives in analysis/ in a repository checkout or an extracted package. Inside an AppImage
the package's own folder is read-only, so the AppImage's AppRun exports
GOW3_DATA_DIR (~/.local/share/tlou-remastered, or the player's own choice) and
everything follows it; the variable also relocates the data outside an AppImage (a
shared data folder, tests). The layout under the root is the same in every case:
  tlou/        the run logs, the boot preparation and the recordings (analysis/tlou)
  tlou-user/   tlou.ini, launcher.json, the saves and the shader cache (analysis/tlou-user)
"""
import os
from pathlib import Path

DATA_DIR_ENV = "GOW3_DATA_DIR"


def data_root(top):
    """The data root for a checkout or package whose top folder is `top`."""
    override = os.environ.get(DATA_DIR_ENV)
    if override:
        return Path(override).expanduser()
    return top / "analysis"
