/**
 * @file icm42688p.cpp
 * @brief ICM-42688-P jiroskop sürücüsü (bkz. header).
 *
 * ⚠ DONANIMDA DENENMEDİ. Sensör elimizde yok; bu dosya yalnız derleniyor.
 * Kayıt adresleri ve ölçek katsayısı veri sayfasından alındı.
 */

#include "icm42688p.h"

#if ENABLE_IMU

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <math.h>

static const char* TAG = "imu";

/* Kayıt haritası (bank 0). */
#define REG_ACCEL_DATA_X1 0x1F
#define REG_GYRO_DATA_Z1  0x29
#define REG_PWR_MGMT0     0x4E
#define REG_GYRO_CONFIG0  0x4F
#define REG_ACCEL_CONFIG0 0x50
#define REG_WHO_AM_I      0x75
#define WHO_AM_I_VALUE    0x47

/* ±2000 derece/sn ölçeğinde 16,4 LSB bir derece/sn'ye denk geliyor. */
#define GYRO_LSB_PER_DPS_X10 164

/* ±16 g ölçeğinde 2048 LSB = 1 g. */
#define ACCEL_LSB_PER_G 2048

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_running = false;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t* dst, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, dst, n, 100);
}

esp_err_t imu_start(void)
{
    if (s_running) return ESP_OK;

    i2c_master_bus_config_t bc = {};
    bc.i2c_port          = IMU_I2C_NUM;
    bc.sda_io_num        = (gpio_num_t)PIN_IMU_SDA;
    bc.scl_io_num        = (gpio_num_t)PIN_IMU_SCL;
    bc.clk_source        = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bc, &s_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c veri yolu kurulamadı: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address  = IMU_I2C_ADDR;
    dc.scl_speed_hz    = 400000;
    err = i2c_master_bus_add_device(s_bus, &dc, &s_dev);
    if (err != ESP_OK) return err;

    uint8_t who = 0;
    err = reg_read(REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sensöre erişilemedi: %s", esp_err_to_name(err));
        return err;
    }
    if (who != WHO_AM_I_VALUE) {
        /* Yanlış sensör ya da yanlış adres. Sessizce devam etmek, ölü hesabın
         * çöp veriyle çalışmasına yol açardı. */
        ESP_LOGE(TAG, "beklenmeyen sensör kimliği 0x%02X (0x%02X bekleniyordu)",
                 who, WHO_AM_I_VALUE);
        return ESP_ERR_NOT_FOUND;
    }

    /* Jiroskop ve ivmeölçeri düşük gürültü kipinde çalıştır. */
    if ((err = reg_write(REG_PWR_MGMT0, 0x0F)) != ESP_OK) return err;
    /* Jiroskop ±2000 derece/sn, 100 Hz. Ölü hesap 1 Hz örnekliyor; yüksek
     * ODR yalnız gürültüyü ortalamak için. */
    if ((err = reg_write(REG_GYRO_CONFIG0, 0x08)) != ESP_OK) return err;
    if ((err = reg_write(REG_ACCEL_CONFIG0, 0x08)) != ESP_OK) return err;

    s_running = true;
    ESP_LOGI(TAG, "init OK (I2C%d, sda=%d scl=%d, adres 0x%02X)",
             IMU_I2C_NUM, PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_ADDR);
    return ESP_OK;
}

esp_err_t imu_stop(void)
{
    if (!s_running) return ESP_OK;
    reg_write(REG_PWR_MGMT0, 0x00);   /* uyku */
    s_running = false;
    return ESP_OK;
}

bool imu_running(void) { return s_running; }

bool imu_read_yaw_rate(int16_t* out_dps_x10)
{
    if (!s_running || !out_dps_x10) return false;

    uint8_t b[2];
    if (reg_read(REG_GYRO_DATA_Z1, b, 2) != ESP_OK) return false;

    int16_t raw = (int16_t)((b[0] << 8) | b[1]);
    int32_t v = ((int32_t)raw * 100) / GYRO_LSB_PER_DPS_X10;

#if IMU_YAW_INVERT
    v = -v;
#endif
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    *out_dps_x10 = (int16_t)v;
    return true;
}

bool imu_read_accel_mag(uint16_t* out_mg)
{
    if (!s_running || !out_mg) return false;

    uint8_t b[6];
    if (reg_read(REG_ACCEL_DATA_X1, b, 6) != ESP_OK) return false;

    int16_t x = (int16_t)((b[0] << 8) | b[1]);
    int16_t y = (int16_t)((b[2] << 8) | b[3]);
    int16_t z = (int16_t)((b[4] << 8) | b[5]);

    double g = sqrt((double)x * x + (double)y * y + (double)z * z) /
               (double)ACCEL_LSB_PER_G;
    double mg = g * 1000.0;
    if (mg > 65535.0) mg = 65535.0;
    *out_mg = (uint16_t)mg;
    return true;
}

#else   /* ENABLE_IMU == 0 — sensör yok */

esp_err_t imu_start(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t imu_stop(void)  { return ESP_OK; }
bool      imu_running(void) { return false; }
bool imu_read_yaw_rate(int16_t* o)  { (void)o; return false; }
bool imu_read_accel_mag(uint16_t* o) { (void)o; return false; }

#endif  /* ENABLE_IMU */
