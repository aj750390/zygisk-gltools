#!/system/bin/sh
SKIPUNZIP=0

ui_print "************************************"
ui_print "*        Zygisk GLTools v1.0       *"
ui_print "*   Next-Gen 3D Graphics Optimizer *"
ui_print "************************************"

# Verify Zygisk is enabled
if [ "$ZYGISK_ENABLED" != "1" ]; then
    ui_print "! Zygisk is not enabled in Magisk/KernelSU!"
    ui_print "! Please enable Zygisk in settings and reboot."
    abort "Installation aborted."
fi

# Create directory structure
ui_print "- Creating config directory..."
mkdir -p /data/adb/modules/zygisk_gltools/config
chmod 777 /data/adb/modules/zygisk_gltools/config

# Install companion app if bundled
if [ -f "$MODPATH/GLToolsCompanion.apk" ]; then
    ui_print "- Installing GLTools Companion Manager..."
    pm install -r "$MODPATH/GLToolsCompanion.apk"
fi

ui_print "- Zygisk GLTools installed successfully!"
ui_print "- Reboot device to activate module."
