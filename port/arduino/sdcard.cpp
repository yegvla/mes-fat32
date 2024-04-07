#include "sdcard.h"

bool sdcard_ready = false;
bool sdcard_is_hcxc = false;
uint8_t sdcard_sector[SD_SECTOR_SIZE];

uint8_t sdcard_calculate_crc7(const uint8_t *data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc =
                (crc & 0x80u) ? ((crc << 1) ^ (SD_CRC_POLY << 1)) : (crc << 1);
        }
    }
    return crc + 1; // setting the end bit
}

uint16_t sdcard_calculate_crc16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0x00;
    for (uint32_t i = 0; i < len; ++i) {
        crc = (uint8_t)(crc >> 8) | (crc << 8);
        crc ^= data[i];
        crc ^= (uint8_t)(crc & 0xff) >> 4;
        crc ^= (crc << 8) << 4;
        crc ^= ((crc & 0xff) << 4) << 1;
    }
    return crc;
}

uint32_t sdcard_args_send_if_cond(bool pcie_1v2, bool pcie_avail,
                                  enum SDVoltageSupplied vhs, uint8_t pattern) {
    return 0x00000000 | (pcie_1v2 << 13) | (pcie_avail << 12) | (vhs << 8) |
           pattern;
}

uint8_t sdcard_transceive(uint8_t data) {
    return SPI.transfer(data);
}

void sdcard_select(void) {
    sdcard_transceive(0xff);
    digitalWrite(SD_NSS, LOW);
    sdcard_transceive(0xff);
}

void sdcard_release(void) {
    sdcard_transceive(0xff);
    digitalWrite(SD_NSS, HIGH);
    sdcard_transceive(0xff);
}

void sdcard_boot_sequence(void) {
    sdcard_release();
    // MOSI needs to be high for at least 74 cycles to enter SPI mode.
    // We send 10 bytes = 80 cycles.
    for (uint8_t i = 0; i < 10; ++i) {
        sdcard_transceive(0xff);
    }
}

void sdcard_send_blocking(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        sdcard_transceive(data[i]);
    }
}

uint8_t sdcard_read(void) {
    return SPI.transfer(0xff);
}

void sdcard_read_buf(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        buf[i] = sdcard_read();
    }
}

void block_while_spi_busy(void) {
    // NOP
}

uint16_t sdcard_read_block(uint8_t *buf, uint32_t len, uint32_t bytes_timeout) {
    uint8_t cursor;
    uint16_t crc = 0;
    while ((cursor = sdcard_transceive(0xff)) == 0xff) {
        bytes_timeout--;
        if (bytes_timeout == 0)
            break;
    }
    if (cursor == SD_BLOCK_START_BYTE) {
        for (uint32_t i = 0; i < len; ++i) {
            *buf++ = sdcard_transceive(0xff);
        }
        // 16bit crc
        crc = sdcard_transceive(0xff) << 8;
        crc |= sdcard_transceive(0xff);
    }
    sdcard_release();
    return crc;
}

void sdcard_send_block(uint8_t *buf, uint32_t len) {
    uint16_t crc = sdcard_calculate_crc16(buf, len);
    sdcard_transceive(0xff);
    sdcard_transceive(SD_BLOCK_START_BYTE);
    for (uint32_t i = 0; i < len; ++i) {
        sdcard_transceive(buf[i]);
    }
    sdcard_transceive((crc >> 8) & 0xff);
    sdcard_transceive(crc & 0xff);
}

uint8_t sdcard_send_command_blocking(uint8_t cmd, uint32_t args,
                                     uint32_t bytes_timeout) {
    uint8_t send[6];
    send[0] = cmd | SD_START_BITS;
    send[1] = (args >> 24) & 0xff;
    send[2] = (args >> 16) & 0xff;
    send[3] = (args >> 8) & 0xff;
    send[4] = args & 0xff;
    send[5] = sdcard_calculate_crc7(send, sizeof(send) - 1);
    sdcard_select();
    sdcard_send_blocking(send, sizeof(send));
    uint8_t response;
    while ((response = sdcard_transceive(0xff)) == 0xff) {
        bytes_timeout--;
        if (bytes_timeout == 0)
            break;
    }
    return response;
}

uint8_t sdcard_send_app_command_blocking(uint8_t cmd, uint32_t args,
                                         uint32_t bytes_timeout) {
    sdcard_send_command_blocking(SD_CMD55_APP_CMD, 0x00000000, 2);
    sdcard_release();
    return sdcard_send_command_blocking(cmd, args, bytes_timeout);
}

void sdcard_establish_spi(uint32_t br) {
    pinMode(SD_NSS, OUTPUT);
    pinMode(SD_MOSI, OUTPUT);
    pinMode(SD_SCK, OUTPUT);
    pinMode(SD_MISO, INPUT);
    SPI.beginTransaction(SPISettings(br, MSBFIRST, SPI_MODE3));
}

void sdcard_set_spi_baudrate(uint32_t br) {
    SPI.beginTransaction(SPISettings(br, MSBFIRST, SPI_MODE3));
}

void sdcard_reset_spi(void) {
    block_while_spi_busy();
    SPI.endTransaction();
}

uint16_t sdcard_request_csd(uint8_t *csd) {
    union SDResponse1 r1;
    r1.repr = sdcard_send_command_blocking(SD_CMD9_SEND_CSD, 0x00000000, 8);
    if (r1.invalid)
        return 0;
    return sdcard_read_block(csd, 16, 8);
}

bool sdcard_request_ocr(uint8_t *ocr) {
    union SDResponse1 r1;
    r1.repr = sdcard_send_command_blocking(SD_CMD58_READ_OCR, 0x00000000, 8);
    if (r1.invalid)
        return false;
    sdcard_read_buf(ocr, 4);
    return true;
}

uint32_t sdcard_calculate_size_csdv1(const uint8_t *csd) {
    uint32_t mult = 2 << (SD_CSDV1_C_SIZE_MULT(csd) + 1);
    uint32_t block_nr = (SD_CSDV1_C_SIZE(csd) + 1) * mult;
    uint32_t block_len = 2 << (SD_CSDV1_READ_BL_LEN(csd) - 1);
    return (block_nr * block_len) / 1000;
}

uint32_t sdcard_calculate_size_csdv2(const uint8_t *csd) {
    return 512 * (SD_CSDV2_C_SIZE(csd) + 1);
}

uint32_t sdcard_calculate_size(const uint8_t *csd) {
    uint8_t ocr[4];
    if (sdcard_request_ocr(ocr)) {
        if (SD_OCR_IS_SDHC_OR_SDXC(ocr)) {
            return sdcard_calculate_size_csdv2(csd);
        } else {
            return sdcard_calculate_size_csdv1(csd);
        }
    }
    return 0;
}

uint8_t sdcard_go_idle(void) {
    return sdcard_send_command_blocking(SD_CMD0_GO_IDLE_STATE, 0x00000000, 256);
}

enum SDInitResult sdcard_init(void) {
    sdcard_ready = false;
    sdcard_is_hcxc = true;
    // sd needs to be powered for around 1ms before starting.
    sdcard_boot_sequence();
    union SDResponse1 r1;
    r1.repr = sdcard_go_idle();
    if (r1.invalid) {
        return SD_CARD_TIMEOUT; // gets returned when to sd failed to answer for
                                // ~1s
    }
    if (!r1.in_idle_state) { // SD-Card should be in idle after commanding it to
                             // go in idle...
        r1.repr = sdcard_go_idle(); // From my experience, sending the command a
                                    // 2nd time sometimes does the trick.
        if (!r1.in_idle_state) {
            return SD_CARD_RESET_ERROR; // give up...
        }
    }
    sdcard_release();
    r1.repr = sdcard_send_command_blocking(
        SD_CMD8_SEND_IF_COND,
        sdcard_args_send_if_cond(false, false, VOLTAGE_2V7_TO_3V6,
                                 SD_CHECK_PATTERN),
        2);
    if (r1.invalid) {
        return SD_CARD_GENERIC_COMMUNICATION_ERROR;
    }

    uint8_t ocr_register[4];

    if (r1.illegal_command) {
        sdcard_is_hcxc = false;
    } else {
        uint8_t sd_status[4];
        sdcard_read_buf(sd_status, sizeof(sd_status));
        const bool pcie_1v2 = (sd_status[2] >> 4) & 2;
        const bool pcie = (sd_status[2] >> 4) & 1;
        const enum SDVoltageSupplied vhs = sd_status[2] & 0xf;
        const uint8_t pattern = sd_status[3];
        if (pattern != SD_CHECK_PATTERN)
            return SD_CARD_GENERIC_COMMUNICATION_ERROR;
        if (vhs != VOLTAGE_2V7_TO_3V6)
            return SD_CARD_TARGET_VOLTAGE_UNSUPPORTED;
        if (pcie)
            return SD_CARD_GENERIC_COMMUNICATION_ERROR;
        if (pcie_1v2)
            return SD_CARD_GENERIC_COMMUNICATION_ERROR;
        sdcard_release();
        r1.repr = sdcard_send_command_blocking(SD_CMD58_READ_OCR, 0x00000000, 2);
        if (r1.invalid)
            return SD_CARD_GENERIC_COMMUNICATION_ERROR;
        sdcard_read_buf(ocr_register, sizeof(ocr_register));
    }
    sdcard_release();
    uint32_t start_time = millis();
    while (r1.in_idle_state) {
        r1.repr = sdcard_send_app_command_blocking(SD_ACMD41_SD_SEND_OP_COND,
                                                   0x40000000, 2);
        sdcard_release();
        delay(10);
        if (millis() > start_time + 1000)
            break;
    }
    if (r1.in_idle_state) {
        return SD_CARD_WAKEUP_TIMEOUT;
    }
    sdcard_release();
    r1.repr = sdcard_send_command_blocking(SD_CMD58_READ_OCR, 0x00000000, 2);
    if (r1.invalid)
        return SD_CARD_GENERIC_COMMUNICATION_ERROR;
    sdcard_read_buf(ocr_register, sizeof(ocr_register));
    if (sdcard_is_hcxc) {
        if (SD_OCR_IS_SDHC_OR_SDXC(ocr_register)) {
            sdcard_is_hcxc = true;
        } else {
            sdcard_is_hcxc = false;
        }
    }
    sdcard_ready = true;
    return SD_CARD_NO_ERROR;
}

bool sdcard_init_peripheral(void) {
    sdcard_reset_spi();
    sdcard_establish_spi(200000);
    enum SDInitResult res = sdcard_init();
    if (res != SD_CARD_NO_ERROR) {
        return false;
    }
    sdcard_set_spi_baudrate(16000000);
    return true;
}

bool sdcard_read_sector(uint32_t sector, uint8_t *data) {
    if (!sdcard_is_hcxc) {
        sector *= SD_SECTOR_SIZE;
    }
    sdcard_send_command_blocking(SD_CMD17_READ_SINGLE_BLOCK, sector, 8);
    uint16_t crc = sdcard_read_block(data, SD_SECTOR_SIZE, 0);
    uint16_t calculated = sdcard_calculate_crc16(data, SD_SECTOR_SIZE);
    if (crc != calculated) {
	return false;
    }
    return true;
}

void sdcard_write_sector(uint32_t sector, uint8_t *data) {
    if (!sdcard_is_hcxc) {
        sector *= SD_SECTOR_SIZE;
    }
    sdcard_send_command_blocking(SD_CMD24_WRITE_BLOCK, sector, 8);
    sdcard_send_block(data, SD_SECTOR_SIZE);
    while (sdcard_transceive(0xff) == 0xff)
        ;
    while (sdcard_transceive(0xff) != 0xff)
        ;
}
