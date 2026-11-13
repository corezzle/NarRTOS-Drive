/**
 * @file can_signals.cpp
 * @brief Tekerlek hızı çıkarımı (bkz. header).
 *
 * Tasarım notu: kayan nokta yok. Çözünürlük 1/256 km/sa; 10 katına çevirmek
 * için ham değeri 10 ile çarpıp 256'ya bölmek yetiyor. Yuvarlama açıkça
 * yazıldı ki host testi ile hedef aynı sonucu versin.
 */

#include "can_signals.h"

uint32_t can_signal_pgn(uint32_t id)
{
    /* 29 bit kimlik: [öncelik 3b][R][DP][PF 8b][PS 8b][SA 8b]
     * PF >= 240 (PDU2): PGN = DP<<16 | PF<<8 | PS
     * PF <  240 (PDU1): PS hedef adrestir, PGN'e girmez. */
    const uint32_t pf = (id >> 16) & 0xFFu;
    const uint32_t ps = (id >> 8)  & 0xFFu;
    const uint32_t dp = (id >> 24) & 0x03u;   /* DP + EDP */

    if (pf >= 240u) return (dp << 16) | (pf << 8) | ps;
    return (dp << 16) | (pf << 8);
}

bool can_signal_wheel_speed(uint32_t id, const uint8_t* data, uint8_t dlc,
                            uint16_t* out_kmh_x10)
{
    if (!data || !out_kmh_x10) return false;
    if (can_signal_pgn(id) != CAN_PGN_CCVS1) return false;
    if (dlc < 3) return false;                  /* sinyal 2. ve 3. baytta */

    const uint16_t raw = (uint16_t)(data[1] | ((uint16_t)data[2] << 8));

    /* J1939'da üst aralık veri değil, durum bildirir:
     *   0xFE00-0xFEFF  hata göstergesi
     *   0xFF00-0xFFFF  veri yok / kullanılamaz
     * Bunları hız sanmak duran araç yanılgısına yol açardı. */
    if (raw >= 0xFE00u) return false;

    /* 1/256 km/sa -> km/sa'in 10 katı, yuvarlanarak. */
    *out_kmh_x10 = (uint16_t)(((uint32_t)raw * 10u + 128u) / 256u);
    return true;
}
