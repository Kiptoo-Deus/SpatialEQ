# dmgbuild settings for the SpatialEQ disk image (drag-to-Applications window with the box art).
#   dmgbuild -s branding/dmg-settings.py -D app=path/to/SpatialEQ.app -D branding=path/to/branding \
#            "SpatialEQ 1.1.1" SpatialEQ-1.1.1.dmg
import os.path

app = defines["app"]  # noqa: F821 (provided by dmgbuild)
here = defines["branding"]  # noqa: F821

format = "UDZO"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = os.path.join(here, "icon", "SpatialEQ.icns")  # the mounted volume's icon
background = os.path.join(here, "installer", "dmg-background.png")  # dmgbuild picks up the @2x file too

window_rect = ((200, 120), (720, 440))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 112
text_size = 13
# Matches the arrow drawn in dmg-background.png.
icon_locations = {
    os.path.basename(app): (420, 220),
    "Applications": (610, 220),
}
