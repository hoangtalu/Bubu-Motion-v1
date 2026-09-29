# Thử nghiệm: tắt soi gương code vào PSRAM (`SPIRAM_FETCH_INSTRUCTIONS` / `SPIRAM_RODATA`)

**Trạng thái: ĐÃ CHẠY 2026-09-22 — TRƯỢT, ĐÃ LÙI.** Máy bàn đã về nguyên trạng
(`elf_sha256 43793869…`, đọc ngược khỏi chip để xác nhận).

## Kết quả

| Chỉ số | Soi gương BẬT (baseline) | Soi gương TẮT | Kết luận |
|---|---|---|---|
| PSRAM free (nghỉ) | 4.083.940 B | **7.431.560 B** | **+3,19 MB — đạt, đúng như dự đoán** |
| PSRAM khối lớn nhất | 3.997.696 B | 7.340.032 B | đạt |
| Internal free / khối lớn nhất | 49.419–52.151 / 43.008 | 49.187–51.911 / 43.008 | không đổi — đạt |
| Boot → idle | 8,91 s | 8,60 s | nhanh hơn 0,31 s — đạt |
| panic / watchdog | 0 | 0 | đạt |
| **Dòng lỗi `E()`** | **0** | **2.529** | **TRƯỢT** |

Lỗi lặp lại suốt: `gdma-link: gdma_link_mount_buffers(173): no more space for buffer mounting`,
dòng đầu tiên **70 ms sau `AudioCodec: Audio codec started`**, rồi ~7 lần/giây không dứt.

Internal free và DMA free **không đổi** giữa hai bản, nên đây không phải thiếu bộ nhớ: đây là
danh sách mô tả (descriptor link list) của DMA quá nhỏ cho các buffer âm thanh nay nằm trong
PSRAM. Âm thanh là sản phẩm, nên một mình lỗi này đủ để trượt.

**Chưa chết hẳn:** 3,19 MB kia không phải bất khả. Ghim buffer âm thanh vào bộ nhớ DMA nội, hoặc
nới link list, rồi đo lại. Nhưng nó không miễn phí như giả thuyết ban đầu tưởng.

---


## Giả thuyết

Hai cấu hình này đang bắt máy chép 3,16 MB `.text` + `.rodata` từ flash vào PSRAM lúc boot
(đo trên bản 1.7.8 local: `.text` 1.999.820 B + `.rodata` 1.311.060 B). Tắt đi thì code chạy
thẳng từ flash qua cache, và PSRAM heap được trả lại chừng đó. Kỳ vọng: free PSRAM lúc ổn định
đi từ ~1,94 MB lên ~5,1 MB.

Cái giá đã biết trước: khi ghi flash (lưu NVS, tải OTA, **ghi đè cả vùng assets**) thì flash
cache bị tắt; code nằm trong PSRAM vẫn chạy, code đọc từ flash thì phải chờ. Đây gần như chắc
chắn là lý do cấu hình này được bật. Nên ca xấu nhất phải thử là **phát tiếng trong lúc đang
ghi flash**, không phải lúc máy rảnh.

## Đường lùi (chốt trước khi đụng phần cứng)

Ba lớp, lớp nào cũng đủ tự nó:

1. **Bản build gốc còn nguyên byte.** `artifacts/releases/bench-backups/xiaozhi-1.7.8-local-15h22-psram-mirror-ON.bin`
   — đã đối chiếu `elf_sha256 43793869…`, trùng khít `build/xiaozhi.bin` (biên dịch 22/9 15:01:15).
   Lùi = flash lại đúng file đó vào `0x20000`.
2. **`sdkconfig` gốc chưa bao giờ bị sửa.** Bản thử nằm ở sdkconfig riêng trong scratchpad, build
   ra thư mục riêng `build-psram-xip/`. Thư mục `build/` và `sdkconfig` của anh không bị chạm tới.
   (Thêm một bản sao phòng thân: `sdkconfig.bak-psram-mirror` và `artifacts/.../sdkconfig-psram-mirror-ON`.)
3. **Không đụng partition, không đụng assets, không đụng OTA.** Chỉ ghi vùng app `0x20000`.
   Fleet ngoài thị trường không liên quan; OTA vẫn là 1.7.8 cũ.

```bash
# LÙI LẠI — chạy là xong, không cần build
esptool.py --chip esp32s3 -p /dev/cu.usbmodem11101 -b 460800 write_flash 0x20000 /Users/judes/Downloads/Bubu-Motion-v1-main/artifacts/releases/bench-backups/xiaozhi-1.7.8-local-15h22-psram-mirror-ON.bin
```

Sau khi lùi, xác minh lại bằng chính công cụ định danh, **không tin chuỗi version**:

```bash
esptool.py --chip esp32s3 -p /dev/cu.usbmodem11101 read_flash 0x20000 0x100 /tmp/onchip.bin && python3 scripts/app_desc.py /tmp/onchip.bin
```

`elf_sha256` phải ra `43793869ef5f14c30c2f27bb8e14078aa54bc389e64df575bbc96967adf423a8`.

## Vì sao phải định danh bằng sha, không bằng version

Hiện có **hai binary khác nhau cùng đóng dấu 1.7.8**: bản trên OTA (3.449.280 B, đẩy 22/9 11:33)
và bản local 15:22 có thêm game `traffic_runner` (3.458.048 B). Máy bàn cũng sẽ không tự cập nhật
vì 1.7.8 == 1.7.8. Bước 1 của quy trình dưới đây là đọc ngược app descriptor khỏi máy để biết
mình đang lùi về đâu.

## Quy trình

| Bước | Làm gì |
|---|---|
| 0 | Cắm máy bàn. Kiểm tra `ls /dev/cu.usbmodem*` |
| 1 | Đọc ngược `app_desc` khỏi máy → biết chính xác bản đang chạy, và đó là đích để lùi về |
| 2 | **Đo baseline** trên đúng bản đó: bắt serial 10 phút, chơi 1 game, gọi wake word 5 lần, nghe SFX |
| 3 | Flash bản thử (`build-psram-xip/xiaozhi.bin`) vào `0x20000`, đọc lại `app_desc` xác nhận đã đổi |
| 4 | Đo lại y hệt bước 2, cộng thêm ca xấu nhất: phát tiếng trong lúc ghi flash |
| 5 | Đối chiếu với bảng ngưỡng dưới. Không đạt → chạy lệnh lùi ở trên |

## Ngưỡng đạt (chốt trước, không sửa sau khi thấy kết quả)

| Chỉ số | Đạt khi | Ai đo |
|---|---|---|
| PSRAM free lúc ổn định | **≥ +2,5 MB** so với baseline | log |
| Internal SRAM free / khối lớn nhất | không tệ hơn baseline | log |
| Boot tới `State: activating -> idle` | không chậm hơn baseline quá 10% | log |
| Ổn định 10 phút | 0 panic, 0 watchdog, 0 reset ngoài ý muốn | log |
| **Tiếng khi đang ghi flash** | không vấp, không khựng | tai người |
| Mắt 30 fps | không giật thấy được | mắt người |
| Wake word | 5/5 lần ăn, độ trễ không tăng rõ | tai người |
| Tiếng rẹt SFX | **không tệ hơn** hiện tại | tai người |

Bốn dòng cuối phải do anh nghe/nhìn — log không nói được. Chỉ cần **một** dòng trượt là lùi.

## Ghi chú

Tiếng rẹt SFX đến nay vẫn chưa ai giải thích được, và hai giả thuyết đã bị phần cứng bác bỏ
(2026-09-07, 2026-09-08) đều là về **chỗ đặt dữ liệu**. Thử nghiệm này đổi **đường đi của code** —
một góc chưa ai chạm vào. Kết quả xấu cũng là dữ liệu: nó thu hẹp chỗ để tìm.
