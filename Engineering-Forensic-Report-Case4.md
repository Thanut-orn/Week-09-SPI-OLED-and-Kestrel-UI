# Engineering Forensic Report
## Case 4: Telemetry Mismatch & Framing Error

### 1. รหัสนักศึกษา / ผู้รายงาน

- ID: 67030109
- ID: 67030085

### 2. กรณีศึกษาข้อผิดพลาด

- [ ] Case 1
- [ ] Case 2
- [ ] Case 3
- [x] Case 4: Telemetry Mismatch & Framing Error

### 3. อาการที่ตรวจพบทางกายภาพ

เมื่อหมุน Potentiometer ค่า ADC ที่ ESP32 และค่าบน OLED เปลี่ยนแปลง แต่ค่าบนเว็บ Kestrel ค้างอยู่ที่ `2048` หรือข้อมูลแสดงผลไม่ต่อเนื่อง ทำให้ค่าบน OLED และเว็บไม่ตรงกัน

### 4. สมมติฐานและการทดสอบเบื้องต้น

สมมติฐานคือข้อมูล Serial ไม่มี packet delimiter หรือฝั่ง Kestrel ไม่ได้อ่านค่าจาก Serial จริง โดยสาเหตุที่ต้องตรวจสอบมีดังนี้:

1. ESP32 ต้องส่งข้อมูลเป็นเฟรมที่จบด้วย `\\n` เช่น `ADC:2192\\n`
2. ESP32 และ Kestrel ต้องใช้ baud rate เดียวกัน คือ `115200`
3. Kestrel ต้องอ่านค่า ADC ล่าสุดจาก `COM12` แทนการใช้ค่าจำลอง `2048`

การทดสอบคือหมุน Potentiometer แล้วตรวจสอบค่า Serial และเรียก `GET /api/telemetry` ซ้ำหลายครั้ง

### 5. หลักฐานที่ตรวจพบจากโค้ดหรือหน่วยความจำ

- ESP32 ส่งข้อมูลด้วย:

  ```c
  printf("ADC:%d\\n", raw_value);
  ```

- ESP32 อ่าน ADC จาก `ADC1_CHANNEL_6` ซึ่งเป็น `GPIO34`
- ESP32 ส่งข้อมูลทุก 50 ms
- Kestrel รับข้อมูลจาก `COM12` ที่ `115200 baud`
- Kestrel ตรวจสอบเฟรมที่ขึ้นต้นด้วย `ADC:` และอ่านข้อมูลทีละบรรทัด
- ค่าจำลองเดิม `int simulatedRaw = 2048;` ถูกแทนที่ด้วยค่า ADC ล่าสุดจาก ESP32
- Kestrel ส่งค่ากลับไปยัง ESP32 ในรูปแบบ:

  ```text
  SET:<percent>:<message>
  ```

หลักฐานจากการทดสอบจริง:

```json
{
  "raw": 2192,
  "calibrated": 53.7,
  "unit": "%",
  "displayMsg": "SYSTEM READY",
  "lastReceivedUtc": "2026-09-15T05:45:08.5896668Z"
}
```

### 6. มาตรการแก้ไขและผลการยืนยันความถูกต้อง

ดำเนินการแก้ไขดังนี้:

1. เพิ่ม `\\n` ท้ายข้อมูล ADC ทุกเฟรม
2. เพิ่ม UART driver และกำหนด UART0 เป็น `115200, 8N1`
3. เพิ่ม `SerialTelemetryService` สำหรับอ่านข้อมูลจาก `COM12`
4. แยกเฟรมด้วย newline และรับเฉพาะรูปแบบ `ADC:<raw>`
5. ส่งค่าที่คำนวณแล้วกลับไปยัง ESP32 ด้วย `SET:<percent>:<message>`
6. เปลี่ยน API telemetry ให้ใช้ค่าจริงจาก ESP32 แทนค่าจำลอง

ผลการยืนยัน:

- Firmware build สำเร็จ
- Firmware flash ลง ESP32 สำเร็จ
- ESP32 อ่านค่า ADC และส่งข้อมูลต่อเนื่อง
- API ตอบกลับ `200 OK`
- ค่า `raw` และ `calibrated` บนเว็บเปลี่ยนตามค่า ADC จริง
- Kestrel ไม่ล่มเมื่อรับข้อมูล Serial

สถานะ: **PASS**

### 7. ไฟล์หลักฐาน

- [ESP32 main.c](67030085/Lab9-1_OLED_BringUp/main/main.c)
- [Kestrel Program.cs](67030085/ESP32.Kestrel.Webserver/Program.cs)
- [SerialTelemetryService.cs](67030085/ESP32.Kestrel.Webserver/Services/SerialTelemetryService.cs)
- [CalibrationService.cs](67030085/ESP32.Kestrel.Webserver/Services/CalibrationService.cs)

### 8. สรุปผลการชันสูตร

ต้นเหตุของ Telemetry Mismatch คือ Kestrel ใช้ค่า ADC จำลองแทนข้อมูลจาก ESP32 และระบบ Serial ต้องอาศัย newline เพื่อระบุขอบเขตของแต่ละเฟรม หลังจากเพิ่ม delimiter, ตั้งค่า UART ให้ตรงกัน และอ่านค่า ADC ล่าสุดจาก `COM12` แล้ว ค่าแสดงผลบน OLED และเว็บสามารถอ้างอิงข้อมูลชุดเดียวกันได้อย่างถูกต้อง.
