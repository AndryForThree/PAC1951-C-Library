#include "PAC1951.h"

#include <stddef.h>
#include <string.h>

#include <ti/sysbios/knl/Clock.h>	// Used to get Timinng functions!
#include <ti/sysbios/knl/Task.h>

#define PAC1951_MAX_WRITE_BYTES 8U

// Safety Check on input parameters
static PAC1951_Status	PAC1951_checkOpen				(PAC1951_Object *dev);
static bool				PAC1951_isValidAddress			(uint8_t address);
static bool				PAC1951_isValidRange			(PAC1951_Range range);
static bool				PAC1951_isValidSampleMode		(PAC1951_SampleMode mode);
static bool				PAC1951_isSampleModeAdaptive	(PAC1951_SampleMode mode);
static bool				PAC1951_isValidAccumulatorMode	(PAC1951_AccumulatorMode mode);
static bool				PAC1951_isValidAlertPinMode		(PAC1951_AlertPinMode mode);
static bool				PAC1951_isValidAlertPin			(PAC1951_AlertPin pin);
static bool				PAC1951_isValidAlertSource		(PAC1951_AlertSource event);


// General Purpose helpers
static void				PAC1951_sleep_ms					(uint32_t delay_ms);
static PAC1951_Status	PAC1951_readDeviceId				(PAC1951_Object *dev, PAC1951_DeviceId *id);

// Read and conversion functions for PAC1951 measuremnts
static PAC1951_Status	PAC1951_readRaw_vBus				(PAC1951_Object *dev, bool useAverage, uint16_t *raw);
static PAC1951_Status	PAC1951_readRaw_vSense				(PAC1951_Object *dev, bool useAverage, uint16_t *raw);
static PAC1951_Status	PAC1951_readRaw_power				(PAC1951_Object *dev, uint32_t *raw);
static PAC1951_Status	PAC1951_readRaw_accumulatorCount	(PAC1951_Object *dev, uint32_t *count);
static PAC1951_Status	PAC1951_readRaw_accumulator			(PAC1951_Object *dev, int64_t *raw);

static int32_t			PAC1951_convertRaw_vBus_mV			(PAC1951_Object *dev, uint16_t rawVbus);
static int32_t			PAC1951_convertRaw_vSense_uV		(PAC1951_Object *dev, uint16_t rawVsense);
static int64_t			PAC1951_convertRaw_power_mW			(PAC1951_Object *dev, int64_t rawPower);

// Read functions with typed output (to simplify the read/write, without the use of array of bytes)
static PAC1951_Status 	PAC1951_readU8						(PAC1951_Object *dev, uint8_t reg, uint8_t *value);
static PAC1951_Status 	PAC1951_readU16						(PAC1951_Object *dev, uint8_t reg, uint16_t *value);
static PAC1951_Status 	PAC1951_readU24						(PAC1951_Object *dev, uint8_t reg, uint32_t *value);
static PAC1951_Status 	PAC1951_readU32						(PAC1951_Object *dev, uint8_t reg, uint32_t *value);
static PAC1951_Status 	PAC1951_writeU8						(PAC1951_Object *dev, uint8_t reg, uint8_t value);
static PAC1951_Status 	PAC1951_writeU16					(PAC1951_Object *dev, uint8_t reg, uint16_t value);
static PAC1951_Status 	PAC1951_writeU24					(PAC1951_Object *dev, uint8_t reg, uint32_t value);

PAC1951_Status PAC1951_init(	PAC1951_Object *dev,
								I2C_Handle i2c,
								uint8_t i2cAddress,
								PAC1951_PartNumber partNumber,
								uint32_t senseResistorUohm) {
	// Check the validity of input pointers (I2C Handle is already a pointer)
    if((dev == NULL) || (i2c == NULL)) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

	// Check if the Configurated I2C Address is valid for PAC1951
    if(!PAC1951_isValidAddress(i2cAddress)) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

	// Check that the Part Number is an accepted one
	if((partNumber != PAC1951_PART_NUMBER_1951_1) && (partNumber != PAC1951_PART_NUMBER_1951_2)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}
	
	// Check that the senseResistor is != 0
	if(senseResistorUohm == 0U) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Initialize the PAC1951 main object as all 0!
    memset(dev, 0, sizeof(*dev));

	// Initilaize the PAC1951 main object with the passed values
    dev->i2c				= i2c;
    dev->i2cAddress			= i2cAddress;
    dev->partNumber			= partNumber;
    dev->senseResistorUohm	= senseResistorUohm;
    dev->vbusRange			= PAC1951_RANGE_UNIPOLAR_FSR;
    dev->vsenseRange		= PAC1951_RANGE_UNIPOLAR_FSR;
    dev->sampleMode			= PAC1951_SAMPLE_1024_ADAPT_ACC;
    dev->accumulatorMode	= PAC1951_ACC_MODE_POWER;
    dev->isOpen				= true;
	dev->accumulationValid	= true;
    return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_close(PAC1951_Object *dev) {
	PAC1951_Status status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

    memset (dev, 0, sizeof(*dev));
	return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_sleep (PAC1951_Object *dev) {
	return PAC1951_setSampleMode(dev, PAC1951_SAMPLE_SLEEP);
}

PAC1951_Status PAC1951_verifyDevice(PAC1951_Object *dev) {
    PAC1951_DeviceId id;
	PAC1951_Status status;

	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Retrive all the IDs of the PAC1951 via I2C
    status = PAC1951_readDeviceId(dev, &id);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

	// Check if the IDs are the expected ones
    if(	(id.manufacturerId != PAC1951_MANUFACTURER_ID_MICROCHIP) || 
		(id.productId != (uint8_t)dev->partNumber)) {
        return PAC1951_STATUS_ID_MISMATCH;
    }

    return PAC1951_STATUS_OK;
}


PAC1951_Status PAC1951_setSampleMode(PAC1951_Object *dev, PAC1951_SampleMode mode) {
	uint16_t ctrlValue;
	uint16_t sampleField;
	uint16_t sampleMask;
	bool newModeAdaptive;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidSampleMode(mode)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the current value of the setting saved in the control register
	status = PAC1951_readU16(dev, PAC1951_REG_CTRL, &ctrlValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Convert the clean enum value into the real value used inside the CTRL register.
	// Sleep is the only case that is not sequential in the datasheet register map.
	sampleField = (uint16_t)mode;
	if(mode == PAC1951_SAMPLE_SLEEP) {
		sampleField = PAC1951_SAMPLE_SLEEP_FIELD_VALUE;
	}
	newModeAdaptive = PAC1951_isSampleModeAdaptive(mode);

	// Modify only the Sample Mode fields, and leave all the remaining bits untouched
	sampleMask = (uint16_t)(PAC1951_SAMPLE_FIELD_MASK << PAC1951_SAMPLE_FIELD_BASE_BIT);
	ctrlValue &= (uint16_t)(~sampleMask);	// Clear the "Sample Mode" field bits
	ctrlValue |= (uint16_t)((sampleField & PAC1951_SAMPLE_FIELD_MASK) << PAC1951_SAMPLE_FIELD_BASE_BIT);	// Write the new "Sample Mode" in the "Sample Mode" field bits

	// Write the updated register back into the PAC1951 register
	status = PAC1951_writeU16(dev, PAC1951_REG_CTRL, ctrlValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new sample mode.
	// If the old accumulation is already invalid and we are going back to an adaptive mode,
	// use a full refresh to restart the accumulator from a clean point.
	if(newModeAdaptive && !dev->accumulationValid) {
		status = PAC1951_refresh(dev);
	}
	else {
		status = PAC1951_refreshV(dev);
	}
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// If the refresh was successful, update the PAC1951 object copy.
	dev->sampleMode = mode;
	dev->accumulationValid = newModeAdaptive;

	return PAC1951_STATUS_OK;
}


PAC1951_Status PAC1951_setMeasurementRange(PAC1951_Object *dev, PAC1951_Range vsenseRange, PAC1951_Range vbusRange) {
	uint16_t regValue;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidRange(vsenseRange) || !PAC1951_isValidRange(vbusRange)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the current value of the range configuration register.
	status = PAC1951_readU16(dev, PAC1951_REG_NEG_PWR_FSR, &regValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Modify only the channel 1 range fields, leaving the other bits untouched.
	regValue &= (uint16_t)~((uint16_t)(0x03U << PAC1951_NEG_PWR_FSR_VSENSE_CH1_SHIFT) |
						   (uint16_t)(0x03U << PAC1951_NEG_PWR_FSR_VBUS_CH1_SHIFT));
	regValue |= (uint16_t)(((uint16_t)vsenseRange) << PAC1951_NEG_PWR_FSR_VSENSE_CH1_SHIFT);
	regValue |= (uint16_t)(((uint16_t)vbusRange) << PAC1951_NEG_PWR_FSR_VBUS_CH1_SHIFT);

	// Write the updated register back into the PAC1951.
	status = PAC1951_writeU16(dev, PAC1951_REG_NEG_PWR_FSR, regValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new range configuration with a full refresh.
	// The accumulator must be reset because old samples used the previous range conversion.
	status = PAC1951_refresh(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// If the refresh was successful, update the PAC1951 object copy.
	dev->vsenseRange = vsenseRange;
	dev->vbusRange = vbusRange;

	return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_setAccumulatorMode(PAC1951_Object *dev, PAC1951_AccumulatorMode mode) {
	uint8_t regValue;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidAccumulatorMode(mode)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the current value of the accumulator configuration register.
	status = PAC1951_readU8(dev, PAC1951_REG_ACC_CFG, &regValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Modify only the channel 1 accumulator mode field, leaving the other bits untouched.
	regValue &= (uint8_t)~(PAC1951_ACC_CFG_FIELD_MASK << PAC1951_ACC_CFG_CH1_SHIFT);
	regValue |= (uint8_t)(((uint8_t)mode & PAC1951_ACC_CFG_FIELD_MASK) << PAC1951_ACC_CFG_CH1_SHIFT);

	// Write the updated register back into the PAC1951.
	status = PAC1951_writeU8(dev, PAC1951_REG_ACC_CFG, regValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new accumulator configuration with a full refresh.
	// Old accumulated samples belong to the previous accumulator mode, so they are not valid anymore.
	status = PAC1951_refresh(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// If the refresh was successful, update the PAC1951 object copy.
	dev->accumulatorMode = mode;

	return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_setActiveChannels(PAC1951_Object *dev, uint16_t channelMask) {
	uint16_t ctrlValue;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if((channelMask & ~PAC1951_ALL_CH_OFF) != 0U) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the current value of the control register.
	status = PAC1951_readU16(dev, PAC1951_REG_CTRL, &ctrlValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Modify only the channel enable bits, leaving the other CTRL fields untouched.
	ctrlValue &= (uint16_t)(~PAC1951_ALL_CH_OFF);
	ctrlValue |= channelMask;

	// Write the updated register back into the PAC1951.
	status = PAC1951_writeU16(dev, PAC1951_REG_CTRL, ctrlValue);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new channel configuration with a full refresh.
	// Changing active channels changes the accumulation history, so restart it cleanly.
	status = PAC1951_refresh(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// If channel 1 is disabled, the channel 1 accumulator can not be considered valid.
	if((channelMask & PAC1951_CH1_OFF) != 0U) {
		dev->accumulationValid = false;
	}

	return PAC1951_STATUS_OK;
}


PAC1951_Status PAC1951_setAlertPinMode(PAC1951_Object *dev, PAC1951_AlertPin alertPin, PAC1951_AlertPinMode pinMode) {
    uint16_t ctrlValue = 0;
	uint8_t regShift;
    PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidAlertPin(alertPin) || !PAC1951_isValidAlertPinMode(pinMode)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the current value of the control register.
    status = PAC1951_readU16(dev, PAC1951_REG_CTRL, &ctrlValue);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

	// Modify only the selected alert pin mode field, leaving the other CTRL bits untouched.
	regShift = PAC1951_ALERT_FIELD_BASE_BIT + (PAC1951_ALERT_FIELD_WIDTH * (uint8_t)alertPin);
	ctrlValue &= (uint16_t)(~(PAC1951_ALERT_FIELD_MASK << regShift));
	ctrlValue |= (uint16_t)(((uint16_t)pinMode & PAC1951_ALERT_FIELD_MASK) << regShift);

	// Apply the new channel configuration
    status = PAC1951_writeU16(dev, PAC1951_REG_CTRL, ctrlValue);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

    return PAC1951_refreshV(dev);
}

PAC1951_Status PAC1951_setAlertSource(PAC1951_Object *dev, PAC1951_AlertSource alertSource, bool enable) {
	uint32_t enableMask;
	uint32_t sourceMask;
	uint8_t sourceBit;
	uint8_t sourceOffset;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidAlertSource(alertSource)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Calculate the real register bit that corresponds to the selected alert source.
	if(alertSource <= PAC1951_ALERT_EVENT_CH1_OVER_POWER) {
		sourceBit = PAC1951_ALERT_CH1_SOURCE_BASE_BIT - (PAC1951_ALERT_CH1_SOURCE_BIT_STEP * (uint8_t)alertSource);
	}
	else {
		sourceOffset = (uint8_t)alertSource - (uint8_t)PAC1951_ALERT_EVENT_ACC_OVERFLOW;
		sourceBit = PAC1951_ALERT_SYSTEM_SOURCE_BASE_BIT - (PAC1951_ALERT_SYSTEM_SOURCE_BIT_STEP * sourceOffset);
	}
	sourceMask = (uint32_t)(1UL << sourceBit);

	// Read the current global alert source enable mask.
	status = PAC1951_readU24(dev, PAC1951_REG_ALERT_ENABLE, &enableMask);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Modify only the selected event bit, leaving the other alert sources untouched.
	if(enable) {
		enableMask |= sourceMask;
	}
	else {
		enableMask &= ~sourceMask;
	}

	// Write the updated ALERT_ENABLE register back into the PAC1951.
	status = PAC1951_writeU24(dev, PAC1951_REG_ALERT_ENABLE, enableMask);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new alert source configuration without clearing the accumulation registers.
	return PAC1951_refreshV(dev);
}

PAC1951_Status PAC1951_setAlertRouting(PAC1951_Object *dev, PAC1951_AlertPin alertPin, PAC1951_AlertSource alertSource, bool enable) {
	uint32_t alertMask;
	uint32_t sourceMask;
	uint8_t sourceBit;
	uint8_t sourceOffset;
	uint8_t alertReg;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(!PAC1951_isValidAlertPin(alertPin) || !PAC1951_isValidAlertSource(alertSource)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Calculate the real register bit that corresponds to the selected alert source.
	if(alertSource <= PAC1951_ALERT_EVENT_CH1_OVER_POWER) {
		sourceBit = PAC1951_ALERT_CH1_SOURCE_BASE_BIT - (PAC1951_ALERT_CH1_SOURCE_BIT_STEP * (uint8_t)alertSource);
	}
	else {
		sourceOffset = (uint8_t)alertSource - (uint8_t)PAC1951_ALERT_EVENT_ACC_OVERFLOW;
		sourceBit = PAC1951_ALERT_SYSTEM_SOURCE_BASE_BIT - (PAC1951_ALERT_SYSTEM_SOURCE_BIT_STEP * sourceOffset);
	}
	sourceMask = (uint32_t)(1UL << sourceBit);

	// ALERT1 and ALERT2 routing registers are consecutive, so the pin enum is used as the register offset.
	// ALERT1 --> SLOW_ALERT1 register, ALERT2 --> GPIO_ALERT2 register.
	alertReg = (uint8_t)(PAC1951_REG_ALERT_ROUTING_BASE + (uint8_t)alertPin);

	// Read the current routing mask for the selected alert pin.
	status = PAC1951_readU24(dev, alertReg, &alertMask);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Modify only the selected event bit, leaving the other routed events untouched.
	if(enable) {
		alertMask |= sourceMask;
	}
	else {
		alertMask &= ~sourceMask;
	}

	// Write the updated routing register back into the PAC1951.
	status = PAC1951_writeU24(dev, alertReg, alertMask);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Apply the new alert routing without clearing the accumulation registers.
	return PAC1951_refreshV(dev);
}


PAC1951_Status PAC1951_sendCommand(PAC1951_Object *dev, uint8_t commandReg) {
    I2C_Transaction transaction;

    PAC1951_Status status = PAC1951_checkOpen(dev);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

	// Initialize the I2C communication
    memset(&transaction, 0, sizeof(transaction));
    transaction.writeBuf		= &commandReg;			// 
    transaction.writeCount		= 1;					// Number of I2C packets (8 bits each) to be sent 
    transaction.readBuf			= NULL;					// 
    transaction.readCount		= 0;					// Number of I2C packets (8 bits each) to be received
    transaction.slaveAddress	= dev->i2cAddress;		// Address of the "receiver" device, the PAC1951

    if(!I2C_transfer(dev->i2c, &transaction)) {
        return PAC1951_STATUS_I2C_ERROR;
    }

    return PAC1951_STATUS_OK;
}


PAC1951_Status PAC1951_readRegister(PAC1951_Object *dev, uint8_t reg, uint8_t *data, uint8_t len) {
    I2C_Transaction transaction;
    PAC1951_Status status = PAC1951_checkOpen(dev);

    if(status != PAC1951_STATUS_OK) {
        return status;
    }

    if((data == NULL) || (len == 0U)) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

    memset(&transaction, 0, sizeof(transaction));
    transaction.writeBuf = &reg;
    transaction.writeCount = 1;
    transaction.readBuf = data;
    transaction.readCount = len;
    transaction.slaveAddress = dev->i2cAddress;

    if(!I2C_transfer(dev->i2c, &transaction)) {
        return PAC1951_STATUS_I2C_ERROR;
    }

    return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_writeRegister(PAC1951_Object *dev, uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t buffer[PAC1951_MAX_WRITE_BYTES + 1U];
    I2C_Transaction transaction;
    PAC1951_Status status = PAC1951_checkOpen(dev);

    if(status != PAC1951_STATUS_OK) {
        return status;
    }

    if((data == NULL) || (len == 0U) || (len > PAC1951_MAX_WRITE_BYTES)) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

    buffer[0] = reg;
    memcpy(&buffer[1], data, len);

    memset(&transaction, 0, sizeof(transaction));
    transaction.writeBuf = buffer;
    transaction.writeCount = (size_t)len + 1U;
    transaction.readBuf = NULL;
    transaction.readCount = 0;
    transaction.slaveAddress = dev->i2cAddress;

    if(!I2C_transfer(dev->i2c, &transaction)) {
        return PAC1951_STATUS_I2C_ERROR;
    }

    return PAC1951_STATUS_OK;
}


PAC1951_Status PAC1951_refresh(PAC1951_Object *dev) {
    PAC1951_Status status = PAC1951_sendCommand(dev, PAC1951_REG_REFRESH);

    if(status == PAC1951_STATUS_OK) {
        PAC1951_sleep_ms(PAC1951_REFRESH_DELAY_MS);
		dev->accumulationValid = PAC1951_isSampleModeAdaptive(dev->sampleMode);
    }

    return status;
}

PAC1951_Status PAC1951_refreshV (PAC1951_Object *dev) {
    PAC1951_Status status = PAC1951_sendCommand(dev, PAC1951_REG_REFRESH_V);

    if(status == PAC1951_STATUS_OK) {
        PAC1951_sleep_ms(PAC1951_REFRESH_DELAY_MS);
    }

    return status;
}




PAC1951_Status PAC1951_read_vBus_mV(PAC1951_Object *dev, bool useAverage, int32_t *vBus_mV) {
    uint16_t raw;
    PAC1951_Status status;

    if(vBus_mV == NULL) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

    status = PAC1951_readRaw_vBus(dev, useAverage, &raw);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

	*vBus_mV = PAC1951_convertRaw_vBus_mV(dev, raw);
	return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_read_current_mA(PAC1951_Object *dev, bool useAverage, int32_t *current_mA) {
    uint16_t raw_vSense;
    int32_t vSense_uV;
    PAC1951_Status status;

    if((dev == NULL) || (current_mA == NULL)) {
        return PAC1951_STATUS_INVALID_ARGUMENT;
    }

    status = PAC1951_readRaw_vSense(dev, useAverage, &raw_vSense);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

	vSense_uV = PAC1951_convertRaw_vSense_uV(dev, raw_vSense);

    if(dev->senseResistorUohm == 0U) {
        return PAC1951_STATUS_CONFIG_ERROR;
    }

    *current_mA = ((int64_t)vSense_uV * 1000L) / dev->senseResistorUohm;

    return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_read_power_mW(PAC1951_Object *dev, int32_t *power_mW) {
	uint32_t rawPower;
	int64_t parsedPower;
	bool isPowerSigned;
	int64_t convertedPower_mW;
	PAC1951_Status status;

	if((dev == NULL) || (power_mW == NULL)) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Read the raw instantaneous power value from the PAC1951.
	status = PAC1951_readRaw_power(dev, &rawPower);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Power can be signed if VBUS or VSENSE is configured in a bipolar range.
	isPowerSigned = (dev->vbusRange != PAC1951_RANGE_UNIPOLAR_FSR) || (dev->vsenseRange != PAC1951_RANGE_UNIPOLAR_FSR);

	// The PAC1951 power value is only 30 bits, so remove the unused upper bits.
	parsedPower = rawPower & 0x3FFFFFFF;

	// If the value is signed and the sign bit is set, extend the sign to the full int64_t.
	if(isPowerSigned && ((parsedPower & ((int64_t)1 << 29)) != 0)) {
		parsedPower |= ~0x3FFFFFFFLL;
	}

	// Convert the parsed raw power value into mW.
	convertedPower_mW = PAC1951_convertRaw_power_mW(dev, parsedPower);

	*power_mW = (int32_t)convertedPower_mW;
	return PAC1951_STATUS_OK;
}





static bool PAC1951_isValidAddress(uint8_t address) {
	// Address must be in the valid range defined by the Datasheet
    return ((address >= PAC1951_MIN_I2C_ADDRESS) && (address <= PAC1951_MAX_I2C_ADDRESS));
}

static bool PAC1951_isValidRange(PAC1951_Range range) {
	return (range < PAC1951_RANGE_COUNT);
}

static bool PAC1951_isValidSampleMode(PAC1951_SampleMode mode) {
	return (mode < PAC1951_SAMPLE_COUNT);
}

static bool PAC1951_isValidAccumulatorMode(PAC1951_AccumulatorMode mode) {
	return (mode < PAC1951_ACC_MODE_COUNT);
}

static bool PAC1951_isValidAlertPinMode(PAC1951_AlertPinMode mode) {
	return (mode < PAC1951_ALERT_MODE_COUNT);
}

static bool PAC1951_isValidAlertPin(PAC1951_AlertPin pin) {
	return (pin < PAC1951_ALERT_PIN_COUNT);
}

static bool PAC1951_isValidAlertSource(PAC1951_AlertSource event) {
	return (event < PAC1951_ALERT_EVENT_COUNT);
}

static PAC1951_Status PAC1951_checkOpen (PAC1951_Object *dev) {
	if(dev == NULL) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	if(!dev->isOpen || (dev->i2c == NULL)) {
		return PAC1951_STATUS_NOT_OPEN;
	}

	if(dev->senseResistorUohm == 0) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}

	return PAC1951_STATUS_OK;
}

static void PAC1951_sleep_ms(uint32_t delay_ms) {
    uint32_t ticks;

    if(delay_ms == 0U) {
        return;
    }

    ticks = ((delay_ms * 1000U) + (Clock_tickPeriod - 1U)) / Clock_tickPeriod;
    if(ticks == 0U) {
        ticks = 1U;
    }

    Task_sleep(ticks);
}

// ==============================================================================================
// Private Helpers
// ==============================================================================================

static PAC1951_Status PAC1951_readDeviceId(PAC1951_Object *dev, PAC1951_DeviceId *id) {
    PAC1951_Status status;

	// Read the Product ID via I2C
    status = PAC1951_readU8	(dev, PAC1951_REG_ID_PRODUCT, &id->productId);
    if (status != PAC1951_STATUS_OK) {
        return status;
    }

	// Read the Manifacturer ID via I2C
    status = PAC1951_readU8	(dev, PAC1951_REG_ID_MANUFACTURER, &id->manufacturerId);
    if (status != PAC1951_STATUS_OK) {
        return status;
    }

	// Read the Revision ID via I2C
    status = PAC1951_readU8	(dev, PAC1951_REG_ID_REVISION, &id->revisionId);
	return status;
}






PAC1951_Status PAC1951_readMeasurement(PAC1951_Object *dev, bool useAverage, PAC1951_Measurement *measurement) {
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if (measurement == NULL) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Initialize the measurements to all 0's, so there is no risk of returning spurious values
	memset(measurement, 0, sizeof(*measurement));

	// Refresh the PAC1951 registers, so the measurements are as updated as possible
	status = PAC1951_refreshV(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Read the physical values using the normal high-level read functions
	status = PAC1951_read_vBus_mV(dev, useAverage, &measurement->vBus_mV);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	status = PAC1951_read_current_mA(dev, useAverage, &measurement->current_mA);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	status = PAC1951_read_power_mW(dev, &measurement->power_mW);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	return PAC1951_STATUS_OK;
}



PAC1951_Status PAC1951_readEnergy_uWh(PAC1951_Object *dev, int64_t *energy_uWh) {
	uint32_t accumulatorCount;
	int64_t accumulator;
	int64_t accumulatedPower_mW;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if (energy_uWh == NULL) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Check if the PAC1951 is set up to accumulate Power values (basically it will accumulate Energy)
	// If the accumulator is not set up in this way, the Energy can not be returned, so generate and error
	if(dev->accumulatorMode != PAC1951_ACC_MODE_POWER) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}
	if(!dev->accumulationValid) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}

	// Refresh the PAC1951 registers, so the measurements are as updated as possible
	status = PAC1951_refreshV(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Read the number of accumulation that were performed by the PAC1951 since the last read
	// If no new Accumulation were performed, just return a 0 as the accumulated value
	status = PAC1951_readRaw_accumulatorCount(dev, &accumulatorCount);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(accumulatorCount == 0) {
		*energy_uWh = 0;
		return PAC1951_STATUS_OK;
	}

	// Read the raw accumulated Power value
	status = PAC1951_readRaw_accumulator(dev, &accumulator);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	accumulatedPower_mW = PAC1951_convertRaw_power_mW(dev, accumulator);

	// accumulatedPower_mW is the sum of all power samples in mW
	// Energy [Ws] 	= Power [W] * Time [s]
	//				= Accumulated_Power [W] * Period_Accumulation [s]
	//				= Accumulated_Power [W] / Freq_Accumulation [1/s]
	//
	// Energy [mWh]	= Energy [Ws] * 1000 / 3600
	// With the Adaptive Accumulation modes, the frequency of the Accumulation is always 1024 SPS (if sampling freq. is less,
	// the accumulated values are multiplied to have an effective accumulation rate of 1024 SPS)
	*energy_uWh = (accumulatedPower_mW * 1000) / (1024 * 3600);
	return PAC1951_STATUS_OK;
}

PAC1951_Status PAC1951_readCharge_uAh(PAC1951_Object *dev, int64_t *charge_uAh) {
	uint32_t accumulatorCount;
	int64_t accumulator;
	int64_t denominator;
	int64_t accumulatedCurrent_uA;
	PAC1951_Status status;

	// Sanity check on the input parameters
	status = PAC1951_checkOpen(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(charge_uAh == NULL) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	// Check if the PAC1951 is set up to accumulate VSENSE values (basically it will accumulate Charge)
	// If the accumulator is not set up in this way, the Charge can not be returned, so generate an error
	if(dev->accumulatorMode != PAC1951_ACC_MODE_VSENSE) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}
	if(!dev->accumulationValid) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}

	// Refresh the PAC1951 registers, so the measurements are as updated as possible
	status = PAC1951_refreshV(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Read the number of accumulation that were performed by the PAC1951 since the last read
	// If no new Accumulation were performed, just return a 0 as the accumulated value
	status = PAC1951_readRaw_accumulatorCount(dev, &accumulatorCount);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}
	if(accumulatorCount == 0U) {
		*charge_uAh = 0;
		return PAC1951_STATUS_OK;
	}

	// Read the raw accumulated VSENSE value
	status = PAC1951_readRaw_accumulator(dev, &accumulator);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	if(dev->vsenseRange == PAC1951_RANGE_BIPOLAR_FSR) {
		denominator = PAC1951_RAW_16_BIPOLAR_DEN;
	}
	else {
		denominator = PAC1951_RAW_16_UNIPOLAR_DEN;
	}

	accumulatedCurrent_uA = (accumulator * PAC1951_VSENSE_FSR_UV * 1000000LL) /
							(denominator * (int64_t)dev->senseResistorUohm);

	// accumulatedCurrent_uA is the sum of all current samples in uA
	// Charge [C] 	= Current [A] * Time [s]
	//				= Accumulated_Current [A] * Period_Accumulation [s]
	//				= Accumulated_Current [A] / Freq_Accumulation [1/s]
	//
	// Charge [uAh]	= Charge [uA*s] / 3600
	// With the Adaptive Accumulation modes, the frequency of the Accumulation is always 1024 SPS (if sampling freq. is less,
	// the accumulated values are multiplied to have an effective accumulation rate of 1024 SPS)
	*charge_uAh = accumulatedCurrent_uA / (1024 * 3600);
	return PAC1951_STATUS_OK;
}



static int32_t PAC1951_convertRaw_vBus_mV(PAC1951_Object *dev, uint16_t rawVbus) {
	switch(dev->vbusRange)
	{
		case PAC1951_RANGE_UNIPOLAR_FSR:
			return ((int64_t)rawVbus * PAC1951_VBUS_FSR_MV) / PAC1951_RAW_16_UNIPOLAR_DEN;

		case PAC1951_RANGE_BIPOLAR_FSR:
			return ((int64_t)(int16_t)rawVbus * PAC1951_VBUS_FSR_MV) / PAC1951_RAW_16_BIPOLAR_DEN;

		case PAC1951_RANGE_BIPOLAR_HALF_FSR:
			return ((int64_t)(int16_t)rawVbus * PAC1951_VBUS_FSR_MV) / PAC1951_RAW_16_UNIPOLAR_DEN;

		default:
			return 0;
	}
}

static int32_t PAC1951_convertRaw_vSense_uV(PAC1951_Object *dev, uint16_t rawVsense) {
	switch(dev->vsenseRange)
	{
		case PAC1951_RANGE_UNIPOLAR_FSR:
			return ((int64_t)rawVsense * PAC1951_VSENSE_FSR_UV) / PAC1951_RAW_16_UNIPOLAR_DEN;

		case PAC1951_RANGE_BIPOLAR_FSR:
			return ((int64_t)(int16_t)rawVsense * PAC1951_VSENSE_FSR_UV) / PAC1951_RAW_16_BIPOLAR_DEN;

		case PAC1951_RANGE_BIPOLAR_HALF_FSR:
			return ((int64_t)(int16_t)rawVsense * PAC1951_VSENSE_FSR_UV) / PAC1951_RAW_16_UNIPOLAR_DEN;

		default:
			return 0;
	}
}

static int64_t PAC1951_convertRaw_power_mW(PAC1951_Object *dev, int64_t rawPower) {
	int64_t denominator;
	int64_t powerFsr_mW;

	int64_t vbusFsr_mV		= PAC1951_VBUS_FSR_MV;
	int64_t vsenseFsr_uV	= PAC1951_VSENSE_FSR_UV;

	if(dev->vbusRange == PAC1951_RANGE_BIPOLAR_HALF_FSR) {
		vbusFsr_mV /= 2;
	}
	if(dev->vsenseRange == PAC1951_RANGE_BIPOLAR_HALF_FSR) {
		vsenseFsr_uV /= 2;
	}

	if((dev->vbusRange != PAC1951_RANGE_UNIPOLAR_FSR) || (dev->vsenseRange != PAC1951_RANGE_UNIPOLAR_FSR)) {
		denominator = PAC1951_RAW_30_BIPOLAR_DEN;
	}
	else {
		denominator = PAC1951_RAW_30_UNIPOLAR_DEN;
	}

	powerFsr_mW = (vbusFsr_mV * vsenseFsr_uV) / (int64_t)dev->senseResistorUohm;
	return (rawPower * powerFsr_mW) / denominator;
}


static PAC1951_Status PAC1951_readRaw_vBus(PAC1951_Object *dev, bool useAverage, uint16_t *raw) {
	uint8_t reg = PAC1951_REG_VBUS_CH1;

	if(useAverage) {
		reg = PAC1951_REG_VBUS_CH1_AVG;
	}

    return PAC1951_readU16(dev, reg, raw);
}

static PAC1951_Status PAC1951_readRaw_vSense(PAC1951_Object *dev, bool useAverage, uint16_t *raw) {
	uint8_t reg = PAC1951_REG_VSENSE_CH1;

	if(useAverage) {
		reg = PAC1951_REG_VSENSE_CH1_AVG;
	}

    return PAC1951_readU16(dev, reg, raw);
}

static PAC1951_Status PAC1951_readRaw_power(PAC1951_Object *dev, uint32_t *raw) {
    uint32_t regValue;
    PAC1951_Status status;

    status = PAC1951_readU32(dev, PAC1951_REG_VPOWER_CH1, &regValue);
    if(status != PAC1951_STATUS_OK) {
        return status;
    }

    *raw = regValue >> 2;

	return PAC1951_STATUS_OK;
}

static PAC1951_Status PAC1951_readRaw_accumulatorCount(PAC1951_Object *dev, uint32_t *count) {
	// Read the register containing the number of accumulated values since the last REFRESH command
	return PAC1951_readU32(dev, PAC1951_REG_ACC_COUNT, count);
}

static PAC1951_Status PAC1951_readRaw_accumulator(PAC1951_Object *dev, int64_t *raw) {
	uint8_t data[7];
	bool signedValue;
	PAC1951_Status status;

	if(!dev->accumulationValid) {
		return PAC1951_STATUS_CONFIG_ERROR;
	}

	// Decide if the accumulated raw value must be treated as signed.
	// The accumulator stores different quantities depending on ACC_CFG, so the sign depends on that mode.
	switch(dev->accumulatorMode) {
		case PAC1951_ACC_MODE_POWER:
			signedValue = (dev->vbusRange != PAC1951_RANGE_UNIPOLAR_FSR) ||
						  (dev->vsenseRange != PAC1951_RANGE_UNIPOLAR_FSR);
			break;

		case PAC1951_ACC_MODE_VSENSE:
			signedValue = (dev->vsenseRange != PAC1951_RANGE_UNIPOLAR_FSR);
			break;

		case PAC1951_ACC_MODE_VBUS:
			signedValue = (dev->vbusRange != PAC1951_RANGE_UNIPOLAR_FSR);
			break;

		default:
			return PAC1951_STATUS_CONFIG_ERROR;
	}

	// Read the 7 bytes that contain the raw accumulated value.
	status = PAC1951_readRegister(dev, PAC1951_REG_VACC_CH1, data, sizeof(data));
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Refresh the PAC1951, so the Accumulator are resetted!
	status = PAC1951_refresh(dev);
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	// Rebuild the accumulator value. The PAC1951 stores multi-byte registers in big-endian order.
	*raw = ((int64_t)data[0] << 48)	|
		   ((int64_t)data[1] << 40)	|
		   ((int64_t)data[2] << 32)	|
		   ((int64_t)data[3] << 24)	|
		   ((int64_t)data[4] << 16)	|
		   ((int64_t)data[5] << 8)	|
		   ((int64_t)data[6]);

	// If the accumulator represents signed samples, extend the sign to the full int64_t
	if(signedValue && ((*raw & ((int64_t)1 << 55)) != 0)) {
		*raw -= ((int64_t)1 << 56);
	}

	return PAC1951_STATUS_OK;
}

static bool PAC1951_isSampleModeAdaptive (PAC1951_SampleMode mode) {
	return mode <= PAC1951_SAMPLE_8_ADAPT_ACC;
}


static PAC1951_Status PAC1951_readU8(PAC1951_Object *dev, uint8_t reg, uint8_t *value) {
	return PAC1951_readRegister(dev, reg, value, 1U);
}

static PAC1951_Status PAC1951_readU16(PAC1951_Object *dev, uint8_t reg, uint16_t *value) {
	uint8_t data[2];
	PAC1951_Status status;

	status = PAC1951_readRegister(dev, reg, data, sizeof(data));
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	*value = ((uint16_t)data[0] << 8) | data[1];
	return PAC1951_STATUS_OK;
}

static PAC1951_Status PAC1951_readU24(PAC1951_Object *dev, uint8_t reg, uint32_t *value) {
	uint8_t data[3];
	PAC1951_Status status;

	status = PAC1951_readRegister(dev, reg, data, sizeof(data));
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	*value = ((uint32_t)data[0] << 16)	|
			((uint32_t)data[1] << 8)	|
			data[2];

	return PAC1951_STATUS_OK;
}

static PAC1951_Status PAC1951_readU32(PAC1951_Object *dev, uint8_t reg, uint32_t *value) {
	uint8_t data[4];
	PAC1951_Status status;

	status = PAC1951_readRegister(dev, reg, data, sizeof(data));
	if(status != PAC1951_STATUS_OK) {
		return status;
	}

	*value = ((uint32_t)data[0] << 24)	|
			((uint32_t)data[1] << 16)	|
			((uint32_t)data[2] << 8)	|
			data[3];

	return PAC1951_STATUS_OK;
}

static PAC1951_Status PAC1951_writeU8(PAC1951_Object *dev, uint8_t reg, uint8_t value) {
	return PAC1951_writeRegister(dev, reg, &value, 1U);
}

static PAC1951_Status PAC1951_writeU16(PAC1951_Object *dev, uint8_t reg, uint16_t value) {
	uint8_t data[2];

	data[0] = (uint8_t)(value >> 8);
	data[1] = (uint8_t)value;

	return PAC1951_writeRegister(dev, reg, data, sizeof(data));
}

static PAC1951_Status PAC1951_writeU24(PAC1951_Object *dev, uint8_t reg, uint32_t value) {
	uint8_t data[3];

	if((value & ~0xFFFFFFUL) != 0U) {
		return PAC1951_STATUS_INVALID_ARGUMENT;
	}

	data[0] = (uint8_t)(value >> 16);
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)value;

	return PAC1951_writeRegister(dev, reg, data, sizeof(data));
}
