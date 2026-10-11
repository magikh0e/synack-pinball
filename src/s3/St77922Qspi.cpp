/* ST77922 over quad SPI. See St77922Qspi.h for why this is not a
 * TFT_eSPI setup file and why it does not need a newer core.
 *
 * The initialisation table below is copied verbatim from the vendor's
 * ST77922 library, by a script, not retyped. It is 63 register writes of
 * undocumented hex and the failure mode of a single wrong nibble is a
 * panel that is dark or tinted with nothing to point at.
 *
 * The transaction shapes are the vendor's too, including two things that
 * look like mistakes and are not:
 *
 *   every pixel chunk resends the opcode and address, because the command
 *   it resends is 0x3C, RAM write continue, which is what that is for, and
 *
 *   the first chunk after a window is set also uses 0x3C rather than 0x2C.
 *   The vendor does this and the panel accepts it. Changing it to the
 *   "correct" command is the sort of improvement that costs an evening.
 */
#include "St77922Qspi.h"

/* Compiled only where it can mean anything. Arduino builds every .cpp in
 * the sketch directory, so without this the CYD build would compile a
 * quad-SPI driver for a panel it does not have. (Pinball: keyed on the
 * build's BOARD_ES3C35P flag, which is defined before any header is read.) */
#if defined(BOARD_ES3C35P)

#include <Arduino.h>
#include "driver/spi_master.h"

/* 16384 pixels, 32 KB, per transfer. The bus is told to expect that much
 * and a 320x480 frame goes out in ten of them. */
#define ST77922_TX_PIXELS 0x4000

#define QSPI_1W_CMD   0x02   /* register write: opcode on one line      */
#define QSPI_4W_CMD   0x32   /* pixel write: data phase on four lines   */
#define WR_RAM_C_CMD  0x3C   /* RAM write continue                      */
#define SET_X_CMD     0x2A
#define SET_Y_CMD     0x2B
#define MADCTL_CMD    0x36

typedef struct {
  uint8_t cmd;
  void *data;
  uint8_t len;
  uint32_t delay_ms;
} lcd_init_cmd;

static const lcd_init_cmd kInit[] = {
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x65, (uint8_t []){0x80}, 1, 0},
    {0x79, (uint8_t []){0x06}, 1, 0},
    {0x7B, (uint8_t []){0x00, 0x08, 0x08}, 3, 0},
    {0x80, (uint8_t []){0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, (uint8_t []){0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, (uint8_t []){0x92, 0x25}, 2, 0},
    {0x87, (uint8_t []){0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, (uint8_t []){0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x8A, (uint8_t []){0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, (uint8_t []){0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, (uint8_t []){0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, (uint8_t []){0x0C}, 1, 0},
    {0x8E, (uint8_t []){0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, (uint8_t []){0x00, 0x30}, 2, 0},
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x71, (uint8_t []){0xD0}, 1, 0},
    {0x66, (uint8_t []){0x02, 0x3F}, 2,  0},
    {0xBE, (uint8_t []){0x26, 0x00, 0x9D}, 3, 0},
    {0x70, (uint8_t []){0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, (uint8_t []){0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27}, 9, 0},
    {0x91, (uint8_t []){0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27}, 9, 0},
    {0x92, (uint8_t []){0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27}, 10, 0},
    {0x93, (uint8_t []){0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27}, 10, 0},
    {0x94, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, (uint8_t []){0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, (uint8_t []){0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0x97, (uint8_t []){0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0xBA, (uint8_t []){0x55, 0x94, 0x2D, 0x94, 0x27}, 5, 0},
    {0x9A, (uint8_t []){0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, (uint8_t []){0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, (uint8_t []){0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, (uint8_t []){0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, (uint8_t []){0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, (uint8_t []){0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, (uint8_t []){0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, (uint8_t []){0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, (uint8_t []){0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, (uint8_t []){0x23, 0x23}, 2, 0},
    {0xBF, (uint8_t []){0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, (uint8_t []){0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, (uint8_t []){0x15, 0x27}, 2, 0},
    {0x7B, (uint8_t []){0x04, 0x57}, 2, 0},
    {0x7E, (uint8_t []){0x01, 0x0E}, 2, 0},
    {0xBF, (uint8_t []){0x36}, 1, 0},
    {0xE3, (uint8_t []){0x40, 0x40}, 2, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xD0, (uint8_t []){0x00}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, (uint8_t []){0x00}, 0, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 0},
    {0x2C, (uint8_t []){0x00}, 0, 0},
    {0x3A, (uint8_t []){0x01}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x01}, 1, 20},
};

static spi_device_handle_t s_dev = nullptr;
static bool s_ready = false;

/* A register write. One-line opcode 0x02, then a 24-bit address whose top
 * byte carries the register, then the parameters. Nothing like TFT_eSPI's
 * command-then-parameters, which is the whole reason this file exists. */
static void writeReg(uint32_t cmd, const void *data, uint8_t len) {
  spi_transaction_ext_t t;
  memset(&t, 0, sizeof(t));
  t.base.flags = (SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR);
  t.base.cmd = QSPI_1W_CMD;
  t.base.addr = cmd << 8;
  t.command_bits = 8;
  t.address_bits = 24;
  if (len) {
    t.base.tx_buffer = data;
    t.base.length = 8 * len;
  }
  spi_device_polling_transmit(s_dev, (spi_transaction_t *)&t);
}

namespace St77922 {

bool ready() { return s_ready; }

bool begin() {
  pinMode(ST77922_BL, OUTPUT);
  digitalWrite(ST77922_BL, LOW);      /* dark until there is something to show */

  spi_bus_config_t bus;
  memset(&bus, 0, sizeof(bus));
  bus.data0_io_num = ST77922_D0;
  bus.data1_io_num = ST77922_D1;
  bus.sclk_io_num  = ST77922_SCLK;
  bus.data2_io_num = ST77922_D2;
  bus.data3_io_num = ST77922_D3;
  bus.max_transfer_sz = (ST77922_TX_PIXELS * 16) + 8;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    return false;
  }

  spi_device_interface_config_t dev;
  memset(&dev, 0, sizeof(dev));
  dev.mode = 0;
  dev.clock_speed_hz = ST77922_HZ;
  dev.spics_io_num = ST77922_CS;      /* the driver drives CS, not us */
  dev.flags = SPI_DEVICE_HALFDUPLEX;
  dev.queue_size = 17;
  if (spi_bus_add_device(SPI3_HOST, &dev, &s_dev) != ESP_OK) {
    return false;
  }

  for (unsigned i = 0; i < sizeof(kInit) / sizeof(kInit[0]); i++) {
    writeReg(kInit[i].cmd, kInit[i].data, kInit[i].len);
    if (kInit[i].delay_ms) delay(kInit[i].delay_ms);
  }
  s_ready = true;
  return true;
}

void setRotation(uint8_t r) {
  if (!s_ready) return;
  /* Only the identity case is wired. Pueo's profile asks for rotation 0,
   * which is this panel's native portrait, so the framebuffer and the
   * panel already agree and nothing has to be transposed. Anything else
   * would need the pixel shuffle the vendor driver does for 1 and 3, and
   * writing that untested would just be a lie with more code in it. */
  uint8_t v = (r == 2) ? (uint8_t)((1 << 6) | (1 << 7)) : (uint8_t)0x00;
  writeReg(MADCTL_CMD, &v, 1);
}

static void setWindow(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey) {
  uint8_t xb[4] = {(uint8_t)(sx >> 8), (uint8_t)(sx & 0xFF),
                   (uint8_t)((ex - 1) >> 8), (uint8_t)((ex - 1) & 0xFF)};
  uint8_t yb[4] = {(uint8_t)(sy >> 8), (uint8_t)(sy & 0xFF),
                   (uint8_t)((ey - 1) >> 8), (uint8_t)((ey - 1) & 0xFF)};
  writeReg(SET_X_CMD, xb, 4);
  writeReg(SET_Y_CMD, yb, 4);
}

void pushFrame(const uint16_t *px) {
  if (!s_ready || !px) return;

  setWindow(0, 0, ST77922_W, ST77922_H);

  spi_transaction_ext_t t;
  memset(&t, 0, sizeof(t));
  size_t left = (size_t)ST77922_W * ST77922_H;
  const uint16_t *p = px;
  bool first = true;

  while (left) {
    size_t n = (left > ST77922_TX_PIXELS) ? ST77922_TX_PIXELS : left;
    if (first) {
      t.base.flags = (SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                      SPI_TRANS_VARIABLE_ADDR);
      t.base.cmd = QSPI_4W_CMD;
      t.base.addr = WR_RAM_C_CMD << 8;
      t.command_bits = 8;
      t.address_bits = 24;
      first = false;
    } else {
      /* command_bits and address_bits persist, so the continue opcode goes
       * out again with each chunk. That is what 0x3C is for. */
      t.base.flags = (SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                      SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY);
    }
    t.base.tx_buffer = p;
    t.base.length = n * 16;
    /* Polling, as the vendor does it, and left alone on purpose.
     *
     * This was briefly changed to spi_device_transmit on the theory that
     * busy-waiting ~30 ms of every 40 ms frame on core 0 would starve
     * that core's idle task and trip a watchdog. The theory was never
     * tested and the change coincided with a boot loop, while polling had
     * demonstrably pushed 2250 frames without complaint. The transmit
     * path also wants DMA-capable memory, and this framebuffer lives in
     * PSRAM, which is a difference between the two calls that the theory
     * did not account for.
     *
     * If idle-task starvation ever does show up, the fix is a longer
     * frame period or partial updates, not a transfer call swapped on a
     * hunch. */
    spi_device_polling_transmit(s_dev, (spi_transaction_t *)&t);
    p += n;
    left -= n;
  }

  /* The backlight comes up once, after a frame exists, and is never
   * touched again. Not in begin(), because the panel's RAM holds whatever
   * it held and the first thing on screen would be garbage. Not per
   * frame either: setup() attaches LEDC to this pin immediately after
   * init to run the brightness control, and a digitalWrite into a pin
   * LEDC owns is at best ignored. */
  static bool lit = false;
  if (!lit) {
    digitalWrite(ST77922_BL, HIGH);
    lit = true;
  }
}

/* Pinball addition: one rectangle, for dirty-rect rendering. The same
 * transaction shapes as pushFrame, over a smaller window. px is w*h
 * contiguous pixels in the panel's byte order and must be DMA-capable. */
void pushRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *px) {
  if (!s_ready || !px || !w || !h) return;

  setWindow(x, y, x + w, y + h);

  spi_transaction_ext_t t;
  memset(&t, 0, sizeof(t));
  size_t left = (size_t)w * h;
  const uint16_t *p = px;
  bool first = true;
  while (left) {
    size_t n = (left > ST77922_TX_PIXELS) ? ST77922_TX_PIXELS : left;
    if (first) {
      t.base.flags = (SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                      SPI_TRANS_VARIABLE_ADDR);
      t.base.cmd = QSPI_4W_CMD;
      t.base.addr = WR_RAM_C_CMD << 8;
      t.command_bits = 8;
      t.address_bits = 24;
      first = false;
    } else {
      t.base.flags = (SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                      SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY);
    }
    t.base.tx_buffer = p;
    t.base.length = n * 16;
    spi_device_polling_transmit(s_dev, (spi_transaction_t *)&t);
    p += n;
    left -= n;
  }
}

/* Pinball addition: the caller lights the backlight once its first full
 * frame is on the panel, for the same reason pushFrame waits. */
void backlight(bool on) { digitalWrite(ST77922_BL, on ? HIGH : LOW); }

}  // namespace St77922

#endif  /* BOARD_ES3C35P */
