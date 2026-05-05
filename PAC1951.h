#ifndef PAC1951_H_
#define PAC1951_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <ti/drivers/I2C.h>

/*
 * PAC1951 driver.
 *
 * This file contains the public interface and the main constants used to control a PAC1951
 * power monitor. The related implementation is in PAC1951.c.
 *
 * The driver is meant for embedded applications where the PAC1951 is used to measure
 * bus voltage, sense voltage/current, power, accumulated energy and accumulated charge.
 * This first version targets a TI microcontroller project, so it uses the TI driver stack:
 *
 * - <ti/drivers/I2C.h> for the I2C handle and I2C transfers;
 * - <ti/sysbios/knl/Clock.h> and <ti/sysbios/knl/Task.h> in PAC1951.c for the refresh delays.
 *
 * The PAC1951 logic is kept separated from the MCU-specific parts as much as possible.
 * To port this driver to a
 * different MCU, the main things that should be changed are the low-level communication and delay
 * functions in PAC1951.c:
 *
 * - PAC1951_readRegister()
 * - PAC1951_writeRegister()
 * - PAC1951_sendCommand()
 * - PAC1951_delay_ms()
 *
 * The higher-level functions should then keep working in the same way, because they only use these
 * basic helpers to access the PAC1951 registers.
 *
 * The API is split in two levels:
 *
 * - Public API: functions such as PAC1951_setSampleMode(), PAC1951_readMeasurement(),
 *   PAC1951_readEnergy_uWh() and PAC1951_setAlertSource(). These are the functions meant for the
 *   normal use of the driver. They do input checks, preserve unrelated register bits, refresh the
 *   PAC1951 when needed, and keep the PAC1951_Object software copy coherent with the device.
 *
 * - Public Advanced API: functions such as PAC1951_readRegister(), PAC1951_writeRegister(),
 *   PAC1951_refresh() and PAC1951_refreshV(). These functions give more direct control over the
 *   device. They are useful when the application needs a custom configuration, wants to write an
 *   entire register at once, or needs tighter control over timing and refresh behavior.
 *
 * For most applications the normal Public API should be enough. The advanced functions are kept
 * available because the PAC1951 has many registers and, in some cases, direct access is the
 * cleanest or fastest solution.
 */

// ================================================================================================
// General constants of the PAC1951
// ============================================================================================== */

// I2C addresses
#define PAC1951_DEFAULT_I2C_ADDRESS			0x10U		// This address (I2C address of PAC1951) is defined by the Resistor connected to the ADDRESSSEL pin
#define PAC1951_MIN_I2C_ADDRESS				0x10U		// Min. acceptable Address (from Datasheet) for PAC1951
#define PAC1951_MAX_I2C_ADDRESS				0x1FU		// Max. acceptable Address (from Datasheet) for PAC1951

// Nominal value of the resistor connected between the SENSE pins. This value is used to convert
// the read Voltage across the SENSE into a Current measurement, using the following formula:
// current_mA = (vsense_uV * 1000) / rsense_uohm
#define PAC1951_DEFAULT_RSENSE_UOHM          0U

#define PAC1951_REFRESH_DELAY_MS			1U			// Time needed by the PAC1951 to refresh itself after a REFRESH command is issued
#define PAC1951_WAKE_FROM_SLEEP_DELAY_MS	5U			// Time needed by the PAC1951 to wake up after Sleep state

#define PAC1951_VBUS_FSR_MV					32000L		// FSR (Full Scale Range, basically the max readable voltage) of the V_BUS measurement
#define PAC1951_VSENSE_FSR_UV				100000L		// FSR (Full Scale Range, basically the max readable voltage) of the V_SENSE measurement

// Constants used for the Measurement conversion, from Raw to Physical measures (Voltage, Current, Power).
// The used formula is basically:
// physical_value = (raw_value * full_scale_value) / raw_denominator
#define PAC1951_RAW_16_UNIPOLAR_DEN         (1 << 16)	// Denominator value of an Unipolar (no sign) 16 Bit measurement conversion
#define PAC1951_RAW_16_BIPOLAR_DEN          (1 << 15)	// Denominator value of an Bipolar (with sign) 16 Bit measurement conversion
#define PAC1951_RAW_30_UNIPOLAR_DEN         (1 << 30)	// Denominator value of an Unipolar (no sign) 30 Bit measurement conversion
#define PAC1951_RAW_30_BIPOLAR_DEN          (1 << 29)	// Denominator value of an Bipolar (with sign) 30 Bit measurement conversion

#define PAC1951_PRODUCT_ID_1951_1			0x78U	// Product ID of the PAC1951_1 (High-Side Measuring version), that must be obtained from the I2C request
#define PAC1951_PRODUCT_ID_1951_2			0x7CU	// Product ID of the PAC1951_2 (Low-Side Measuring version), that must be obtained from the I2C request
#define PAC1951_MANUFACTURER_ID_MICROCHIP	0x54U	// Manifacturer ID of the PAC1951 that must be obtained from the I2C request

// ==============================================================================================
// PAC1951 Register map
// ==============================================================================================

#define PAC1951_REG_REFRESH					0x00U	// Control Reg. used to force the PAC1951 to refresh/update the measured values, and reset the accumulated values
#define PAC1951_REG_CTRL					0x01U	// Control Reg. with the main PAC1951 controls: sample mode, channel enable/disable and alert pin mode
#define PAC1951_REG_ACC_COUNT				0x02U	// Data Reg. with the number of samples accumulated internally by the PAC1951 for Power/Energy accumulation
#define PAC1951_REG_VACC_CH1				0x03U	// Data Reg. with the accumulated power/energy value (channel 1)
#define PAC1951_REG_VBUS_CH1				0x07U	// Data Reg. with the raw instantaneous V_BUS measurement (channel 1)
#define PAC1951_REG_VSENSE_CH1				0x0BU	// Data Reg. with the raw instantaneous V_SENSE measurement (channel 1)
#define PAC1951_REG_VBUS_CH1_AVG			0x0FU	// Data Reg. with the raw average V_BUS measurement (channel 1)
#define PAC1951_REG_VSENSE_CH1_AVG			0x13U	// Data Reg. with the raw average V_SENSE measurement (channel 1)
#define PAC1951_REG_VPOWER_CH1				0x17U	// Data Reg. with the raw power measurement, calculated from VBUS and VSENSE (channel 1)
#define PAC1951_REG_SMBUS_CFG				0x1CU	// Control Reg. with SMBus/I2C configuration register
#define PAC1951_REG_NEG_PWR_FSR				0x1DU	// Control Reg. with bipolar/unipolar full scale range for power measurements
#define PAC1951_REG_REFRESH_G				0x1EU	// Control Reg. used to "Refresh" that also updates the general configuration
#define PAC1951_REG_REFRESH_V				0x1FU	// Control Reg. used to "Refresh" that updates only the voltage/current values
#define PAC1951_REG_SLOW					0x20U	// Control Reg. used to configure the SLOW/alert timing behavior
#define PAC1951_REG_CTRL_ACT				0x21U	// Data Reg. with an "active copy" of the CTRL Reg., used to check the currently in used configuration
#define PAC1951_REG_NEG_PWR_FSR_ACT			0x22U	// Data Reg. with an "active copy" of NEG_PWR_FSR Reg., used to check the currently in used configuration
#define PAC1951_REG_CTRL_LAT				0x23U	// Data Reg. with an latched copy of CTRL Reg., kept by the device after a refresh/latch event
#define PAC1951_REG_NEG_PWR_FSR_LAT			0x24U	// Data Reg. with an latched copy of NEG_PWR_FSR Reg., kept by the device after a refresh/latch event
#define PAC1951_REG_ACC_CFG					0x25U	// Control Reg. Accumulator configuration register
#define PAC1951_REG_ALERT_STATUS			0x26U	// Data Reg. with the informations about which alert condition happened
#define PAC1951_REG_SLOW_ALERT1				0x27U	// Control Reg. with configurations for the SLOW/ALERT1 pin
#define PAC1951_REG_GPIO_ALERT2				0x28U	// Control Reg. with configurations for the GPIO/ALERT2 pin
#define PAC1951_REG_ALERT_ROUTING_BASE		PAC1951_REG_SLOW_ALERT1	// First alert routing register; ALERT2 is the next one
#define PAC1951_REG_ACC_FULLNESS_LIM		0x29U	// Control Reg. with the limit used to generate an alert when the accumulator is almost full
#define PAC1951_REG_OC_LIM_CH1				0x30U	// Control Reg. with "Over-current" limit threshold (channel 1)
#define PAC1951_REG_UC_LIM_CH1				0x34U	// Control Reg. with "Under-current" limit threshold (channel 1)
#define PAC1951_REG_OP_LIM_CH1				0x38U	// Control Reg. with "Over-power" limit threshold (channel 1)
#define PAC1951_REG_OV_LIM_CH1				0x3CU	// Control Reg. with "Over-voltage" limit threshold (channel 1)
#define PAC1951_REG_UV_LIM_CH1				0x40U	// Control Reg. with "Under-voltage" limit threshold (channel 1)
#define PAC1951_REG_OC_LIM_NSAMPLES			0x44U	// Control Reg. with the number of samples needed before generating an over-current alert
#define PAC1951_REG_UC_LIM_NSAMPLES			0x45U	// Control Reg. with the number of samples needed before generating an under-current alert
#define PAC1951_REG_OP_LIM_NSAMPLES			0x46U	// Control Reg. with the number of samples needed before generating an over-power alert
#define PAC1951_REG_OV_LIM_NSAMPLES			0x47U	// Control Reg. with the number of samples needed before generating an over-voltage alert
#define PAC1951_REG_UV_LIM_NSAMPLES			0x48U	// Control Reg. with the number of samples needed before generating an under-voltage alert
#define PAC1951_REG_ALERT_ENABLE			0x49U	// Control Reg. to enable/disable the different alert sources
#define PAC1951_REG_ACC_CFG_ACT				0x4AU	// Data Reg. with an "active copy" of ACC_CFG Reg., used to check the currently in used configuration
#define PAC1951_REG_ACC_CFG_LAT				0x4BU	// Data Reg. with an latched copy of ACC_CFG Reg., kept by the device after a refresh/latch event
#define PAC1951_REG_ID_PRODUCT				0xFDU	// Data Reg. with Product ID, used to check that the connected device is really a PAC1951
#define PAC1951_REG_ID_MANUFACTURER			0xFEU	// Data Reg. with Manufacturer ID, used to check that the device is from Microchip
#define PAC1951_REG_ID_REVISION				0xFFU	// Data Reg. with Revision ID, useful to know the silicon/device revision

// ==============================================================================================
// CTRL register settings
// ==============================================================================================

// Sampling mode field
#define PAC1951_SAMPLE_FIELD_BASE_BIT		12U		// First bit of the sample mode field inside CTRL
#define PAC1951_SAMPLE_FIELD_WIDTH			4U		// Width of the sample mode field
#define PAC1951_SAMPLE_FIELD_MASK			0x0FU	// Mask for the sample mode field
#define PAC1951_SAMPLE_SLEEP_FIELD_VALUE	0x0FU	// Raw CTRL field value used by the PAC1951 for sleep mode

// Alert pin mode fields
#define PAC1951_ALERT_FIELD_BASE_BIT	8U		// First bit of the ALERT1 mode field inside CTRL
#define PAC1951_ALERT_FIELD_WIDTH		2U		// Width of one alert pin mode field
#define PAC1951_ALERT_FIELD_MASK		0x03U	// Mask for one alert pin mode field

// PAC1951 SENSE channel setting (turn ON-OFF avaiable SENSE channels)
#define PAC1951_CH1_OFF				0x0080		// Option to turn PAC1951 SENSE channel 1 OFF
#define PAC1951_CH2_OFF				0x0040		// Option to turn PAC1951 SENSE channel 2 OFF
#define PAC1951_CH3_OFF				0x0020		// Option to turn PAC1951 SENSE channel 3 OFF
#define PAC1951_CH4_OFF				0x0010		// Option to turn PAC1951 SENSE channel 4 OFF
#define PAC1951_ONLY_CH1_ON			(PAC1951_CH2_OFF	| \
									PAC1951_CH3_OFF		| \
									PAC1951_CH4_OFF) 		// Keeps only channel 1 enabled, because PAC1951 uses just one channel
#define PAC1951_ALL_CH_OFF			0x00F0       // Turns OFF all channels
#define PAC1951_ALL_CH_ON			0x0000       // Turns ON all channels

// ================================================================================================
// NEG_PWR_FSR register settings
// ============================================================================================== */

#define PAC1951_NEG_PWR_FSR_VSENSE_CH1_SHIFT	14U		// Position of the VSENSE channel 1 range field inside NEG_PWR_FSR
#define PAC1951_NEG_PWR_FSR_VBUS_CH1_SHIFT		6U		// Position of the VBUS channel 1 range field inside NEG_PWR_FSR

// ================================================================================================
// ACC_CFG register settings
// ============================================================================================== */

#define PAC1951_ACC_CFG_CH1_SHIFT			6U		// Position of the channel 1 accumulator config field
#define PAC1951_ACC_CFG_FIELD_MASK			0x03U	// Mask for one accumulator config field

// ================================================================================================
// ALERT_ENABLE register settings
// ============================================================================================== */

#define PAC1951_ALERT_CH1_SOURCE_BASE_BIT		23U	// Bit position of the first channel 1 alert source
#define PAC1951_ALERT_CH1_SOURCE_BIT_STEP		4U	// Distance between channel 1 alert source bits
#define PAC1951_ALERT_SYSTEM_SOURCE_BASE_BIT	3U	// Bit position of the first system alert source
#define PAC1951_ALERT_SYSTEM_SOURCE_BIT_STEP	1U	// Distance between system alert source bits

// ================================================================================================
// SMBUS_CFG register settings
// ============================================================================================== */

#define PAC1951_SMBUS_INT_PIN_MASK			0x80U		// Bit that reports/configures the INT pin behavior
#define PAC1951_SMBUS_SLOW_PIN_MASK			0x40U		// Bit that reports/configures the SLOW pin behavior
#define PAC1951_SMBUS_ALERT_MASK			0x20U		// Bit related to the SMBus alert function
#define PAC1951_SMBUS_POR_MASK				0x10U		// Power-On Reset flag, useful to know if the device has restarted
#define PAC1951_SMBUS_TIMEOUT_ON			0x08U		// Enables the SMBus timeout feature
#define PAC1951_SMBUS_BYTE_COUNT_ON			0x04U		// Enables the byte count in block reads/writes
#define PAC1951_SMBUS_AUTO_INC_SKIP_OFF		0x02U		// Controls auto-increment behavior when reading consecutive registers
#define PAC1951_SMBUS_I2C_HIGH_SPEED		0x01U		// Enables high-speed I2C mode if the bus supports it

// ================================================================================================
// Types
// ============================================================================================== */

typedef enum {
    PAC1951_STATUS_OK 				= 0,
    PAC1951_STATUS_ERROR 			= -1,
    PAC1951_STATUS_INVALID_ARGUMENT	= -2,
    PAC1951_STATUS_NOT_OPEN			= -3,
    PAC1951_STATUS_I2C_ERROR		= -4,
    PAC1951_STATUS_CONFIG_ERROR		= -5,
    PAC1951_STATUS_ID_MISMATCH		= -6
} PAC1951_Status;

// Range for the BUS and SENSE measureents. This setting can be, in general, different for the
// BUS and SENSE measurement
typedef enum {
    PAC1951_RANGE_UNIPOLAR_FSR		= 0,	// VBUS: 0..32 V; VSENSE: 0..100 mV
    PAC1951_RANGE_BIPOLAR_FSR		= 1,	// VBUS: +/-32 V; VSENSE: +/-100 mV
    PAC1951_RANGE_BIPOLAR_HALF_FSR	= 2,	// VBUS: +/-16 V; VSENSE: +/-50 mV
    PAC1951_RANGE_COUNT						// Number of valid range options, used for input checks
} PAC1951_Range;

typedef enum {
    PAC1951_SAMPLE_1024_ADAPT_ACC		= 0,	// Adaptive accumulation Mode, with 1024 Samples Per Second (SPS)
    PAC1951_SAMPLE_256_ADAPT_ACC 		= 1,	// Adaptive accumulation Mode, with 256 Samples Per Second (SPS)
    PAC1951_SAMPLE_64_ADAPT_ACC			= 2,	// Adaptive accumulation Mode, with 64 Samples Per Second (SPS)
    PAC1951_SAMPLE_8_ADAPT_ACC			= 3,	// Adaptive accumulation Mode, with 8 Samples Per Second (SPS)
    PAC1951_SAMPLE_1024_SPS				= 4,	// Continuous conversion Mode, with 1024 Samples Per Second (SPS)
    PAC1951_SAMPLE_256_SPS				= 5,	// Continuous conversion Mode, with 256 Samples Per Second (SPS)
    PAC1951_SAMPLE_64_SPS				= 6,	// Continuous conversion Mode, with 64 Samples Per Second (SPS)
    PAC1951_SAMPLE_8_SPS				= 7,	// Continuous conversion Mode, with 8 Samples Per Second (SPS)
    PAC1951_SAMPLE_SINGLE_SHOT			= 8,	// One-Shot sampling Mode. It takes one measurement and stops
    PAC1951_SAMPLE_SINGLE_SHOT_8X		= 9,	// 8x One-Shot sampling Mode. It takes 8 measurement, averages them and stops
    PAC1951_SAMPLE_FAST					= 10,	// Fast sampling mode
    PAC1951_SAMPLE_BURST				= 11,	// Burst mode
    PAC1951_SAMPLE_SLEEP				= 12,	// Sleep mode, used to reduce current consumption
	PAC1951_SAMPLE_COUNT						// Number of valid sampling modes
} PAC1951_SampleMode;

typedef enum {
	PAC1951_ACC_MODE_POWER	= 0,	// Accumulator stores VPOWER samples, useful for energy
	PAC1951_ACC_MODE_VSENSE	= 1,	// Accumulator stores VSENSE samples, useful for charge/coulomb counting
	PAC1951_ACC_MODE_VBUS	= 2,	// Accumulator stores VBUS samples, useful for long voltage averages
	PAC1951_ACC_MODE_COUNT			// Number of valid accumulator modes
} PAC1951_AccumulatorMode;

typedef enum {
	PAC1951_PART_NUMBER_1951_1	= PAC1951_PRODUCT_ID_1951_1,	// PAC1951-1 high-side measuring version
	PAC1951_PART_NUMBER_1951_2	= PAC1951_PRODUCT_ID_1951_2		// PAC1951-2 low-side measuring version
} PAC1951_PartNumber;

typedef enum {
    PAC1951_ALERT_MODE_ALERT			= 0,	// Pin used as a normal PAC1951 alert output
    PAC1951_ALERT_MODE_DIGITAL_INPUT	= 1,	// Pin used as a digital input
    PAC1951_ALERT_MODE_DIGITAL_OUTPUT	= 2,	// Pin used as a digital output
    PAC1951_ALERT_MODE_SLOW				= 3,	// Pin used as the SLOW output
	PAC1951_ALERT_MODE_COUNT				// Number of valid alert pin modes
} PAC1951_AlertPinMode;

typedef enum {
	PAC1951_ALERT_PIN_1		= 0,	// SLOW/ALERT1 pin
	PAC1951_ALERT_PIN_2		= 1,	// GPIO/ALERT2 pin
	PAC1951_ALERT_PIN_COUNT			// Number of valid alert pins
} PAC1951_AlertPin;

typedef enum {
	PAC1951_ALERT_EVENT_CH1_OVER_CURRENT	= 0,	// Channel 1 current above OC limit
	PAC1951_ALERT_EVENT_CH1_UNDER_CURRENT	= 1,	// Channel 1 current below UC limit
	PAC1951_ALERT_EVENT_CH1_OVER_VOLTAGE	= 2,	// Channel 1 bus voltage above OV limit
	PAC1951_ALERT_EVENT_CH1_UNDER_VOLTAGE	= 3,	// Channel 1 bus voltage below UV limit
	PAC1951_ALERT_EVENT_CH1_OVER_POWER		= 4,	// Channel 1 power above OP limit
	PAC1951_ALERT_EVENT_ACC_OVERFLOW		= 5,	// Accumulator fullness limit reached
	PAC1951_ALERT_EVENT_ACC_COUNT			= 6,	// Accumulator count limit reached
	PAC1951_ALERT_EVENT_CONVERSION_COMPLETE	= 7,	// Conversion cycle completed
	PAC1951_ALERT_EVENT_COUNT						// Number of valid alert events
} PAC1951_AlertSource;

// Settings used before opening the PAC1951 driver
typedef struct {
    uint8_t productId;
    uint8_t manufacturerId;
    uint8_t revisionId;
} PAC1951_DeviceId;

typedef struct {
    int32_t vBus_mV;
    int32_t current_mA;
    int32_t power_mW;
} PAC1951_Measurement;

// Struct contianing all the run-time informations about the PAC1951, after instantiation
// Data kept by the driver while the PAC1951 is actually in use
typedef struct {
	I2C_Handle i2c;					
	uint8_t i2cAddress;				
	PAC1951_PartNumber partNumber;
	uint32_t senseResistorUohm;			
	PAC1951_Range vbusRange;		
	PAC1951_Range vsenseRange;		
	PAC1951_SampleMode sampleMode;
	PAC1951_AccumulatorMode accumulatorMode;
	bool isOpen;
	bool accumulationValid;
} PAC1951_Object;

// ==============================================================================================
// Public API
// ==============================================================================================
// General functionalities of the Driver
PAC1951_Status PAC1951_init					(	PAC1951_Object*		dev,
												I2C_Handle			i2c, 
												uint8_t				i2cAddress,
												PAC1951_PartNumber	partNumber,
												uint32_t			senseResistorUohm);
PAC1951_Status PAC1951_close				(PAC1951_Object *dev);
PAC1951_Status PAC1951_sleep				(PAC1951_Object *dev);
PAC1951_Status PAC1951_verifyDevice			(PAC1951_Object *dev);

// Configure the PAC1951 measurement settings.
// These functions update only the requested field and preserve the other bits in the same register.
// For this reason they usually perform a "read --> modify --> write" operation over I2C, followed 
// by a PAC1951 refresh command (and so the related PAC1951_REFRESH_DELAY_MS delay).
// If several fields in the same register must be changed at once and timing is important, it is more
// efficient to build the complete register value and write it once with the low-level helpers.
PAC1951_Status PAC1951_setSampleMode		(PAC1951_Object *dev, PAC1951_SampleMode mode);
PAC1951_Status PAC1951_setMeasurementRange	(PAC1951_Object *dev, PAC1951_Range vsenseRange, PAC1951_Range vbusRange);
PAC1951_Status PAC1951_setAccumulatorMode	(PAC1951_Object *dev, PAC1951_AccumulatorMode mode);
PAC1951_Status PAC1951_setActiveChannels	(PAC1951_Object *dev, uint16_t channelMask);

// Configure Alert Functionalities of PAC1951
PAC1951_Status PAC1951_setAlertPinMode		(PAC1951_Object *dev, PAC1951_AlertPin alertPin, PAC1951_AlertPinMode pinMode);
PAC1951_Status PAC1951_setAlertSource		(PAC1951_Object *dev, PAC1951_AlertSource alertSource, bool enable);
PAC1951_Status PAC1951_setAlertRouting		(PAC1951_Object *dev, PAC1951_AlertPin alertPin, PAC1951_AlertSource alertSource, bool enable);

// Read measurements from the PAC1951
PAC1951_Status PAC1951_readMeasurement		(PAC1951_Object *dev, bool useAverage, PAC1951_Measurement *measurement);
PAC1951_Status PAC1951_readEnergy_uWh		(PAC1951_Object *dev, int64_t *energy_uWh);
PAC1951_Status PAC1951_readCharge_uAh		(PAC1951_Object *dev, int64_t *charge_uAh);

// ==============================================================================================
// Public Advanced API
// ==============================================================================================

// Function to send most important commands to the PAC1951
PAC1951_Status PAC1951_sendCommand			(PAC1951_Object *dev, uint8_t commandReg);
PAC1951_Status PAC1951_refresh				(PAC1951_Object *dev);
PAC1951_Status PAC1951_refreshV				(PAC1951_Object *dev);

// Low-Level functions to access internal Registers (these shall be used for advanced programs, 
// where timing and/or advanced functionalities are necessary)
PAC1951_Status PAC1951_readRegister			(PAC1951_Object *dev, uint8_t reg, uint8_t *data, uint8_t len);
PAC1951_Status PAC1951_writeRegister		(PAC1951_Object *dev, uint8_t reg, const uint8_t *data, uint8_t len);

PAC1951_Status PAC1951_read_vBus_mV			(PAC1951_Object *dev, bool useAverage, int32_t *vBus_mV);
PAC1951_Status PAC1951_read_current_mA		(PAC1951_Object *dev, bool useAverage, int32_t *current_mA);
PAC1951_Status PAC1951_read_power_mW		(PAC1951_Object *dev, int32_t *power_mW);

#ifdef __cplusplus
}
#endif

#endif /* PAC1951_H_ */
