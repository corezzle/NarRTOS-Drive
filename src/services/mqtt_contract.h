#pragma once
/**
 * @file mqtt_contract.h
 * @brief ⚠ FİLO SÖZLEŞMESİ — BİLİNMEYENLERİN TOPLANDIĞI TEK DOSYA.
 *
 * Broker adresi, konu şeması ve kimlik doğrulama HENÜZ BİLİNMİYOR.
 * Araç takip sunucusu veritabanından *okuyor*; veritabanına *yazan*
 * ingestion servisi bu projede yok.
 *
 * Bu yüzden aşağıdakiler bizim TAHMİNİMİZ. Kasten tek dosyada toplandı:
 * gerçek sözleşme öğrenilince mqtt_service.cpp'ye dokunmadan yalnız burası
 * değişecek. Servis kodunun içine gömülmüş bir konu dizesi, sözleşme
 * gelince her yeri taramak demekti.
 *
 * ── Bilinen taraf (sunucu sözleşmesi, TAHMİN DEĞİL) ────────────────────────
 *
 * Cihazlar IMEI ile anahtarlı. Telemetri dokümanının alanları:
 *
 *     imei, dateTime, latitude, longitude, speed, ignitionState, can
 *
 * `can` bir DİZE DİZİSİ: ["0x18FEE500,01,02,...,08", ...] — J1939 çözümü
 * sunucuda `candied` + 5,7 MB'lık DBC ile yapılıyor. Yerini alacağımız cihaz
 * Vehicore THX. Yükün bu alanlara oturması M4'ün işi; mqtt_service yükü
 * yorumlamaz, opak bayt olarak taşır.
 *
 * ── Tahmin ettiğimiz taraf ─────────────────────────────────────────────────
 *
 * Konu şeması: <önek>/<imei>/<yaprak>. IMEI'nin konuda olması neredeyse kesin
 * (ingestion'ın cihazı ayırt etmesi gerekiyor) ama önek ve yaprak adları
 * uydurma.
 *
 * DOĞRULANMASI GEREKENLER:
 *   1. broker adresi ve portu (TLS mi? 8883 ise sertifika da gerekir)
 *   2. konu öneki ve yaprak adları
 *   3. kimlik doğrulama: kullanıcı/şifre mi, istemci sertifikası mı, token mı
 *   4. istemci kimliği (client id) IMEI mi, başka bir şey mi
 *   5. yük JSON mu, yoksa ikili bir protokol mü
 *   6. tek rapor mu, toplu (batch) mu — store&forward biriktirince toplu
 *      göndermek çok daha verimli olurdu
 */

#include <stdio.h>

/* Konu öneki. Ürün ailesi adı içinde: aynı broker'a NarRTOS-Wear de
 * bağlanabilir, karışmasın. */
#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "narrtos/drive"
#endif

/* Yaprak adları. */
#define MQTT_LEAF_TELEMETRY "telemetry"
#define MQTT_LEAF_STATUS    "status"      /* retained + LWT */
#define MQTT_LEAF_CMD       "cmd"         /* cihaz DİNLER */
#define MQTT_LEAF_CMD_REPLY "cmd/reply"   /* cihaz YANITLAR */

/* Telemetri QoS 1. QoS 0 kaybı sessiz kılardı ve store&forward'ın pop'u
 * onaya bağlı (bkz. store_forward.h) — onay yoksa kayıt silinemez.
 * QoS 2'ye gerek yok: aynı raporun iki kez gelmesi sunucuda zararsız,
 * dateTime ile tekilleştirilebilir. */
#define MQTT_QOS_TELEMETRY 1
#define MQTT_QOS_STATUS    1
#define MQTT_QOS_CMD       1

/* Durum konusu retained: sunucu paneli cihaza bakınca son bilinen durumu
 * görsün, sonraki rapora kadar beklemesin. */
#define MQTT_RETAIN_STATUS true

/* Last Will — broker bağlantı koptuğunu fark ettiğinde bunu yayınlar.
 * Araç takibinde "cihaz sustu" ile "araç durdu"yu ayırmanın en ucuz yolu. */
#define MQTT_LWT_PAYLOAD "offline"
#define MQTT_STATUS_ONLINE "online"

/** <önek>/<imei>/<yaprak> kur. IMEI boşsa "unknown" yazılır — konu geçerli
 *  kalsın, sorun broker günlüğünde görünsün. */
static inline void mqtt_topic(char* out, size_t cap, const char* imei,
                              const char* leaf)
{
    snprintf(out, cap, MQTT_TOPIC_PREFIX "/%s/%s",
             (imei && imei[0]) ? imei : "unknown", leaf);
}
