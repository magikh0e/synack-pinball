/* ST77922 over quad SPI, on the Arduino 2.0.x core.
 *
 * Driver for the LCDwiki ES3C35P's 3.5" ESP32-S3 panel.
 *
 * Why this exists rather than a TFT_eSPI setup file: the panel's interface
 * is strapped in hardware by the ST77922's IM2/IM1/IM0 pins, and this
 * module straps them to QSPI (011). 4-line 8-bit serial, which is the only
 * thing TFT_eSPI speaks, is a different strapping (001) and cannot be
 * selected in software. The protocol differs in shape as well as width: a
 * register write is an 8-bit opcode plus a 24-bit address carrying the
 * register, where TFT_eSPI sends a command byte and then parameters. So no
 * User_Setup.h could have reached this panel.
 *
 * Why it does not need a core bump: the quad transaction API this uses,
 * spi_transaction_ext_t with SPI_TRANS_MODE_QIO and the VARIABLE_CMD and
 * VARIABLE_ADDR flags, is already in ESP-IDF 4.4 and therefore already in
 * arduino-esp32 2.0.10. The vendor's own driver needs core 3.x only
 * because it is written against IDF 5's newer wrappers, not because the
 * transport demands them, so this builds on the 2.0.x core.
 *
 * Commands and pixels go out differently, and deliberately. Neither
 * SPI_TRANS_MULTILINE_CMD nor _ADDR is set, so the opcode and address are
 * clocked on one line while the data phase is quad. That is the vendor's
 * arrangement and the panel's expectation, not an optimisation.
 *
 * The initialisation table and the transaction shapes are transcribed from
 * the vendor's ST77922 library, verbatim. Nothing in them is tidied or
 * second-guessed: the table is 69 undocumented register writes and the
 * only thing worse than not understanding it would be editing it.
 */
#ifndef ST77922_QSPI_H
#define ST77922_QSPI_H

#include <stdint.h>

/* From the vendor's ST77922 library header, and they match the vendor IO
 * table (ESP32-S3_IO资源分配表.xlsx): QSPI D0-D3 on IO11/13/14/9, clock IO12,
 * chip select IO10 (active low), backlight IO41. */
#define ST77922_W      320
#define ST77922_H      480
#define ST77922_CS     10
#define ST77922_BL     41
#define ST77922_SCLK   12
#define ST77922_D0     11
#define ST77922_D1     13
#define ST77922_D2     14
#define ST77922_D3      9
#define ST77922_HZ     80000000

namespace St77922 {

/* Brings up the bus, runs the init table, lights the backlight.
 * False if the SPI bus or device would not open, which is the only part of
 * this that reports anything: the panel itself cannot be read back. */
bool begin();

/* MADCTL. Rotation 0 is the panel's native portrait and is what the
 * table uses, so nothing here transposes pixels. */
void setRotation(uint8_t r);

/* A whole 320x480 frame of RGB565, chunked to the bus's transfer limit. */
void pushFrame(const uint16_t *px);

/* Whether begin() got as far as a usable device. */
bool ready();

/* Pinball additions: a w*h rectangle at (x, y), and the backlight. */
void pushRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *px);
void backlight(bool on);

}  // namespace St77922

#endif  // ST77922_QSPI_H
