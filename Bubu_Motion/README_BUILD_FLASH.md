# Hướng dẫn Build và Flash code cho Xiaozhi ESP32

Tài liệu này hướng dẫn cách để biên dịch (build) và nạp (flash) code cho bo mạch ESP32 trong dự án Xiaozhi.

## Yêu cầu môi trường

- **Hệ điều hành:** Linux (Ubuntu/Debian) hoặc WSL trên Windows, hoặc macOS.
- **Python:** Python 3.10 trở lên.
- **ESP-IDF:** Phiên bản 5.x (dự án này sử dụng ESP-IDF 5.5).

---

## 1. Cài đặt ESP-IDF (Nếu chưa có)

Nếu bạn chưa cài đặt ESP-IDF, hãy thực hiện các bước sau:

```bash
# Cài đặt các thư viện cần thiết (Ubuntu/Debian)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Tải ESP-IDF về máy
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git

# Chạy script cài đặt
cd ~/esp/esp-idf
./install.sh esp32,esp32s3
```

---

## 2. Chuẩn bị môi trường (Mỗi lần mở Terminal mới)

Trước khi cấu hình hoặc biên dịch, bạn phải nạp môi trường ESP-IDF vào Terminal:

```bash
# Thay đổi đường dẫn tới đúng thư mục esp-idf của bạn
source ~/esp/esp-idf/export.sh
```
> **Lưu ý:** Trong project này, thư mục `esp-idf` thường được để ở `/home/ekai2/Desktop/xaiozhi/esp/esp-idf/export.sh`, nên lệnh sẽ là:
> `source /home/ekai2/Desktop/xaiozhi/esp/esp-idf/export.sh`

---

## 3. Cấu hình (Menuconfig) [Tùy chọn]

Nếu bạn cần tinh chỉnh các thông số hệ thống, cấu hình wifi, phân vùng (partitions), màn hình, v.v.

```bash
# Di chuyển vào thư mục chứa code esp32
cd /home/ekai2/Desktop/xaiozhi/xiaozhi-doremon/xiaozhi-esp32

# Mở giao diện cấu hình
idf.py menuconfig
```

Nhấn `Q` để thoát và `Y` để lưu sau khi đã cấu hình xong.

---

## 4. Biên dịch (Build) project

Di chuyển vào thư mục chứa mã nguồn ESP32 và chạy lệnh build:

```bash
cd /home/ekai2/Desktop/xaiozhi/xiaozhi-doremon/xiaozhi-esp32
idf.py build
```
Quá trình biên dịch lần đầu tiên có thể mất từ vài phút đến hơn mười phút tùy cấu hình máy tính của bạn. Các lần build sau sẽ nhanh hơn.

---

## 5. Nạp code (Flash) và Xem Log (Monitor)

Kết nối bo mạch ESP32 với máy tính qua cáp USB. Xác định cổng Serial (ví dụ `/dev/ttyUSB0` hoặc `/dev/ttyACM0` trên Linux).

Chạy lệnh sau để vừa nạp vừa mở màn hình theo dõi Log:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```
*(Thay `/dev/ttyUSB0` bằng cổng kết nối thực tế trên máy tính của bạn. Nếu bạn không truyền `-p`, ESP-IDF sẽ tự động cố gắng tìm cổng phù hợp).*

Để thoát màn hình xem Log, bấm tổ hợp phím: `Ctrl` + `]`

---

## Tổng kết câu lệnh nhanh

Nếu môi trường đã sẵn sàng và bo mạch đang cắm trên máy tính, bạn chỉ cần gộp lại chạy 1 lệnh từ trong thư mục `xiaozhi-esp32`:

```bash
source /home/ekai2/Desktop/xaiozhi/esp/esp-idf/export.sh && idf.py build flash monitor
```
