#ifndef SDCARD_LIB
#define SDCARD_LIB

#include "Arduino.h"
#include "SPI.h"

#define SD_MOSI 11
#define SD_MISO 12
#define SD_SCK 13
#define SD_NSS 10

#define SD_SECTOR_SIZE 512
#define SD_CRC_POLY 0b10001001
#define SD_CMD0_GO_IDLE_STATE 0
#define SD_CMD2_ALL_SEND_CID 2
#define SD_CMD1_SEND_OP_COND 1
#define SD_CMD8_SEND_IF_COND 8
#define SD_CMD9_SEND_CSD 9
#define SD_CMD12_STOP_TRANSMISSION 12
#define SD_CMD16_SET_BLOCK_LEN 16
#define SD_CMD17_READ_SINGLE_BLOCK 17
#define SD_CMD18_READ_MULTIPLE_BLOCK 18
#define SD_CMD24_WRITE_BLOCK 24
#define SD_CMD25_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD55_APP_CMD 55
#define SD_CMD58_READ_OCR 58
#define SD_ACMD41_SD_SEND_OP_COND 41

#define SD_OCR_VDD_2V7_2V8(X) (X[2] & 0b10000000)
#define SD_OCR_VDD_2V8_2V9(X) (X[1] & 0b00000001)
#define SD_OCR_VDD_2V9_3V0(X) (X[1] & 0b00000010)
#define SD_OCR_VDD_3V0_3V1(X) (X[1] & 0b00000100)
#define SD_OCR_VDD_3V1_3V2(X) (X[1] & 0b00001000)
#define SD_OCR_VDD_3V2_3V3(X) (X[1] & 0b00010000)
#define SD_OCR_VDD_3V3_3V4(X) (X[1] & 0b00100000)
#define SD_OCR_VDD_3V4_3V5(X) (X[1] & 0b01000000)
#define SD_OCR_VDD_3V5_3V6(X) (X[1] & 0b10000000)
#define SD_OCR_IS_SDHC_OR_SDXC(X) (X[0] & 0b01000000)
#define SD_OCR_POWER_UP_STATUS(X) (X[0] & 0b10000000)

#define SD_CSDV1_TRAN_SPEED(X) ((uint8_t)X[3])
#define SD_CSDV1_CCC(X) ((uint8_t)(X[4] << 4) | ((X[5] & 0xf0) >> 4))
#define SD_CSDV1_READ_BL_LEN(X) ((uint8_t)X[5] & 0x0f)
#define SD_CSDV1_READ_BL_PARTIAL(X) (bool)((X[6] & 0b10000000) >> 7)
#define SD_CSDV1_WRITE_BLK_MISALIGN(X) (bool)((X[6] & 0b01000000) >> 6)
#define SD_CSDV1_READ_BLK_MISALIGN(X) (bool)((X[6] & 0b00100000) >> 5)
#define SD_CSDV1_READ_DSR_IMP(X) (bool)((X[6] & 0b00010000) >> 4)
// next 2 bits reserved...                               0b00001100
#define SD_CSDV1_C_SIZE(X)                                                     \
    ((((uint16_t)X[6] & 0b00000011) << 10) | ((uint16_t)X[7] << 2) |           \
     ((X[8] & 0b11000000) >> 6))
#define SD_CSDV1_VDD_R_CURR_MIN(X) (((uint8_t)X[8] & 0b00111000) >> 3)
#define SD_CSDV1_VDD_R_CURR_MAX(X) ((uint8_t)X[8] & 0b00000111)
#define SD_CSDV1_VDD_W_CURR_MIN(X) (((uint8_t)X[9] & 0b11100000) >> 5)
#define SD_CSDV1_VDD_W_CURR_MAX(X) (((uint8_t)X[9] & 0b00011100) >> 2)
#define SD_CSDV1_C_SIZE_MULT(X)                                                \
    ((((uint8_t)X[9] & 0b00000011) << 1) | (((uint8_t)X[10] & 0b10000000) >> 7))

#define SD_CSDV2_TRAN_SPEED(X) ((uint8_t)X[3])
#define SD_CSDV2_CCC(X) ((uint8_t)(X[4] << 4) | ((X[5] & 0xf0) >> 4))
#define SD_CSDV2_READ_BL_LEN(X) ((uint8_t)X[5] & 0x0f)
#define SD_CSDV2_READ_BL_PARTIAL(X) (bool)((X[6] & 0b10000000) >> 7)
#define SD_CSDV2_WRITE_BLK_MISALIGN(X) (bool)((X[6] & 0b01000000) >> 6)
#define SD_CSDV2_READ_BLK_MISALIGN(X) (bool)((X[6] & 0b00100000) >> 5)
#define SD_CSDV2_READ_DSR_IMP(X) (bool)((X[6] & 0b00010000) >> 4)
// next 6 bits reserved... 6: 0b00001111 7: 0b11000000
#define SD_CSDV2_C_SIZE(X)                                                     \
    (uint32_t)((((uint32_t)X[7] & 0b00111111) << 10) | ((uint32_t)X[8] << 8) | \
               ((uint32_t)X[9]))

#define SD_START_BITS (0b01000000)

#define SD_BLOCK_START_BYTE 0xfe

// can be anything, but this pattern was recommended in spec page 40 of
// version 2.00.
#define SD_CHECK_PATTERN (0b10101010)

/**
 * Returned by @related sdcard_init().
 */
enum SDInitResult {
    SD_CARD_NO_ERROR = 0,
    SD_CARD_TIMEOUT = 1,
    SD_CARD_RESET_ERROR = 2,
    SD_CARD_GENERIC_COMMUNICATION_ERROR = 3,
    SD_CARD_TARGET_VOLTAGE_UNSUPPORTED = 4,
    SD_CARD_WAKEUP_TIMEOUT = 5
};

/**
 * Voltage bitmap enum.
 */
enum SDVoltageSupplied {
    NOT_DEFINED = 0b0000,
    VOLTAGE_2V7_TO_3V6 = 0b0001,
    LOW_VOLTAGE = 0b0010,
    RESERVED = 0b0100,
    RESERVED2 = 0b1000,
} __attribute__ ((packed));

/**
 * R1 bitmap
 */
union SDResponse1 {
    struct {
        unsigned in_idle_state : 1;
        unsigned erase_reset : 1;
        unsigned illegal_command : 1;
        unsigned crc_error : 1;
        unsigned sequence_error : 1;
        unsigned address_error : 1;
        unsigned parameter_error : 1;
        unsigned invalid : 1;
    } __attribute__ ((packed));

    uint8_t repr;
};

/**
 * Calculate the 7 bit CRC value of the given data.
 * The data will be shifted by one bit to the left and the first bit will be set
 * to 1.
 * @param data The data to be processed.
 * @param len The size of the data that should be processed.
 * @return 7 bit CRC
 */
uint8_t sdcard_calculate_crc7(const uint8_t *data, uint32_t len);

uint16_t sdcard_calculate_crc16(const uint8_t *data, uint32_t len);

/**
 * Will calculate the size of the SD-Card with the CSD Register according to
 * Specification 1.00.
 * @related For Specification 2.00 see sdcard_calculate_size_csdv2.
 * @param csd The CSD Register @related sdcard_request_csd.
 * @return Size in kb (kilobytes).
 */
uint32_t sdcard_calculate_size_csdv1(const uint8_t *csd);

/**
 * Will calculate the size of the SD-Card with the CSD Register according to
 * Specification 2.00.
 * @related For Specification 1.00 see sdcard_calculate_size_csdv1.
 * @note Size is specified in kb (kilobytes)
 * @param csd The CSD Register @related sdcard_request_csd.
 * @return Size in kb (kilobytes)
 */
uint32_t sdcard_calculate_size_csdv2(const uint8_t *csd);

/**
 * Will execute either sdcard_calculate_size_csdv1 or
 * sdcard_calculate_size_csdv2, depending on the SD-Card.
 * @related sdcard_calculate_size_csdv1
 * @related sdcard_calculate_size_csdv2
 * @param csd
 * @return
 */
uint32_t sdcard_calculate_size(const uint8_t *csd);

void sdcard_reset_spi(void);

/**
 * Establish the SPI peripheral for communicating with the Card.
 * @note The initial baud-rate should be around 100kHz-400kHz.
 *          This can be speed up after the initialization via @related
 * sdcard_set_spi_baudrate, if required.
 * @param br
 */
void sdcard_establish_spi(uint32_t br);

/**
 * Change the baud-rate after already initializing SPI.
 * Used to speed up SPI after Card initialization is done.
 * @param br The new preferred baud-rate.
 */
void sdcard_set_spi_baudrate(uint32_t br);

/**
 * This function will construct a Argument for CMD8 using the passed arguments.
 * @param pcie_1v2 Is pcie 1V2 available?
 * @param pcie_avail Is pcie available?
 * @param vhs Voltage level.
 * @param pattern Pattern to check communication, can be anything @related
 * SD_CHECK_PATTERN
 * @return Constructed 32bit argument with the parameters passed to the
 * function.
 */
uint32_t sdcard_args_send_if_cond(bool pcie_1v2, bool pcie_avail,
                                  enum SDVoltageSupplied vhs, uint8_t pattern);

/**
 * Will set NSS low, and transmitting 0xff before and after the change.
 * Some cards will not notice the change off NSS if not clocking.
 */
void sdcard_select(void);

/**
 * Will set NSS high, and transmitting 0xff before and after the change.
 * Some cards will not notice the change off NSS if not clocking.
 */
void sdcard_release(void);

/**
 * This function is very similar to @related sdcard_transceive().
 * Unlike sdcard_transceive(), this function does not take a argument and will
 * read first before transmitting any bytes. If there are no bytes to be read,
 * is will transmit 0xff. This function is best used sequentially.
 * @return Byte that was read.
 */
uint8_t sdcard_read(void);

/**
 * Will read a buffer via @related sdcard_read.
 * @param buf The buffer the data should be read into.
 * @param len The length of the data that should be read.
 */
void sdcard_read_buf(uint8_t *buf, uint32_t len);

/**
 * Read data of size @param len into @param buf, allowing for @param
 * bytes_timeout timeout bytes before cancelling.
 * @param buf The buffer for incoming data.
 * @param len The length of incoming data.
 * @param bytes_timeout How many timeout bytes (0xff) should be read before
 * cancelling. Use 0 for no timeout.
 * @return
 */
uint16_t sdcard_read_block(uint8_t *buf, uint32_t len, uint32_t bytes_timeout);

void sdcard_send_block(uint8_t *buf, uint32_t len);

/**
 * This function will block while SPI is busy.
 */
void block_while_spi_busy(void);

/**
 * Write a byte and receive a byte with one operation.
 * This function will block while SPI is busy.
 * @param data byte to be transmitted
 * @return byte recieved.
 */
uint8_t sdcard_transceive(uint8_t data);

/**
 * Will transmit @param len bytes from @param data.
 * The data will be transmitted as is.
 * @param data Data to be transmitted
 * @param len Length of data to transmit.
 */
void sdcard_send_blocking(const uint8_t *data, uint32_t len);

/**
 * @param cmd Any CMD
 * @param args Argument data
 * @param bytes_timeout How many timeout bytes (0xff) should be read until
 * cancelling the operation. Use 0 for no timeout.
 * @return The first byte not 0xff, usually @related SDResponse1
 */

uint8_t sdcard_send_command_blocking(uint8_t cmd, uint32_t args,
                                     uint32_t bytes_timeout);

/**
 * CMD55 gets sent with a timeout of 2 bytes.
 * Will prepend CMD55 (APP_CMD) before sending an Application command (ACMD).
 * @param cmd Any ACMD
 * @param args Argument data
 * @param bytes_timeout How many timeout bytes (0xff) should be read until
 * cancelling the operation. Use 0 for no timeout.
 * @return The first byte not 0xff, usually @related SDResponse1
 */
uint8_t sdcard_send_app_command_blocking(uint8_t cmd, uint32_t args,
                                         uint32_t bytes_timeout);

/**
 * Will clock SCK 80 times, opposed by the minimum of 74 documented in the SD
 * spec.
 */
void sdcard_boot_sequence(void);

/**
 * Sends a GO_IDLE command.
 * @return R1 Response
 */
uint8_t sdcard_go_idle(void);

/**
 * Will make the SD-Card operational, enabling functions like:
 * `sdcard_request_csd`.
 * @return Possible errors that occurred while initializing, or 0
 * (SD_CARD_NO_ERROR) when no error occurred.
 * @related SDInitResult
 */
enum SDInitResult sdcard_init(void);

/**
 * Will read the csd register from the SD-Card.
 * @param csd pointer that's pointing to at least 16 bytes of free space.
 * This will read the 128bit CSD Register /of the connected SD-Card into @param
 * csd
 * @return CRC
 */
uint16_t sdcard_request_csd(uint8_t *csd);

/**
 * Will read the ocr register from the SD-Card.
 * @param ocr pointer that's pointing to at least 4 bytes of free space.
 * @return true if ocr was read.
 */
bool sdcard_request_ocr(uint8_t *ocr);

/**
 * This function will properly setup the SD-Card and display information about
 * it on the connected Display. Therefore the GPU must have been initialized
 * beforehand.
 * @return Was the peripheral initialized successfully?
 */
bool sdcard_init_peripheral(void);

/**
 * Will read 512 bytes of @param data from sector @param sector.
 * @related sdcard_read_sector_partially
 * @param sector The sector to read from, first sector is 0.
 * @param data Where the data should be stored to.
 * @return does the CRC match with the data?
 */
bool sdcard_read_sector(uint32_t sector, uint8_t *data);

/**
 * Will write 512 bytes of @param data into sector @param sector.
 * @related sdcard_read_sector
 * @param sector The sector to read from, first sector is 0.
 * @param data The to be written data.
 */
void sdcard_write_sector(uint32_t sector, uint8_t *data);

extern bool sdcard_ready;
extern bool sdcard_is_hcxc;
extern uint8_t sdcard_sector[SD_SECTOR_SIZE];

#endif /* SDCARD_LIB */

