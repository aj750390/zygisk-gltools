#!/system/bin/sh
SKIPUNZIP=0

ui_print "- Installing Zygisk GLTools v1.0.0..."
mkdir -p /data/adb/gltools/packages
chmod 755 /data/adb/gltools
chmod 755 /data/adb/gltools/packages
ui_print "- Zygisk GLTools installed successfully."
