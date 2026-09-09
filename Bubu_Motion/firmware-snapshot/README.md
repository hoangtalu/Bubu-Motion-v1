# Firmware snapshot

Captured: 2026-09-09 02:35 UTC
PROJECT_VER at capture time: 1.7.2

These are the exact build outputs from the local working tree at the time of this
backup (`idf.py build` artifacts), ready to reflash directly with:

```
esptool.py --chip esp32s3 -p <PORT> write_flash \
  0x0 bootloader.bin \
  0x10000 partition-table.bin \
  0x1e000 ota_data_initial.bin \
  0x20000 xiaozhi.bin
```

(Adjust offsets per `partitions/` if they differ. See DEVLOG.md for the state of
testing/verification this exact build had at capture time.)
