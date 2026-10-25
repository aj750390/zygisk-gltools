#!/system/bin/sh
SKIPUNZIP=0

ui_print "************************************"
ui_print "*        Zygisk GLTools v1.0       *"
ui_print "*   Next-Gen 3D Graphics Optimizer *"
ui_print "************************************"

if [ "$ZYGISK_ENABLED" != "1" ]; then
    ui_print "! Note: Ensure Zygisk is enabled in Magisk settings."
fi

ui_print "- Setting up configuration directories..."
mkdir -p /data/adb/modules/zygisk_gltools/config
chmod -R 777 /data/adb/modules/zygisk_gltools/config
chcon -R u:object_r:system_file:s0 /data/adb/modules/zygisk_gltools

mkdir -p /data/local/tmp/gltools
chmod -R 777 /data/local/tmp/gltools

if [ -f "$MODPATH/GLToolsCompanion.apk" ]; then
    ui_print "- Installing GLTools Companion Manager..."
    pm install -r "$MODPATH/GLToolsCompanion.apk" >/dev/null 2>&1 || true
fi

ui_print "- Zygisk GLTools installed successfully!"
ui_print "- Reboot device to activate module."
