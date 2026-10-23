#!/system/bin/sh
MODDIR=${0%/*}

# Ensure config permissions on boot
mkdir -p /data/adb/modules/zygisk_gltools/config
chmod -R 777 /data/adb/modules/zygisk_gltools/config
chcon -R u:object_r:system_file:s0 /data/adb/modules/zygisk_gltools
