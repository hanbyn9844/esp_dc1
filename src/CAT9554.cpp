#include <Wire.h>
#include "CAT9554.h"
#include "Debug.h"

bool CAT9554::updateGpio = false;

uint32_t i2c_buffer = 0;
bool I2cValidRead(uint8_t addr, uint8_t reg, uint8_t size)
{
    uint8_t retry = I2C_RETRY_COUNTER;
    bool status = false;
    uint8_t last_err = 0;

    i2c_buffer = 0;
    while (!status && retry)
    {
        if (retry != I2C_RETRY_COUNTER)
        {
            delay(2);
        }
        Wire.beginTransmission(addr);
        Wire.write(reg);
        last_err = Wire.endTransmission(false);
        if (last_err != 0)
        {
            Wire.beginTransmission(addr);
            Wire.write(reg);
            last_err = Wire.endTransmission(true);
        }
        if (0 == last_err)
        {
            if (Wire.requestFrom((int)addr, (int)size) == size)
            {
                for (uint32_t i = 0; i < size; i++)
                {
                    i2c_buffer = i2c_buffer << 8 | Wire.read();
                }
                status = true;
            }
        }
        retry--;
    }
    if (!status)
    {
        Debug::AddError(PSTR("I2cR a=0x%02X r=0x%02X err=%d"), addr, reg, last_err);
    }
    return status;
}

bool I2cWrite(uint8_t addr, uint8_t reg, uint32_t val, uint8_t size)
{
    uint8_t retry = I2C_RETRY_COUNTER;
    uint8_t err = 0;
    while (retry)
    {
        if (retry != I2C_RETRY_COUNTER)
        {
            delay(2);
        }
        Wire.beginTransmission(addr);
        Wire.write(reg);
        uint8_t bytes = size;
        while (bytes--)
        {
            Wire.write((val >> (8 * bytes)) & 0xFF);
        }
        err = Wire.endTransmission(true);
        if (err == 0)
        {
            return true;
        }
        retry--;
    }
    Debug::AddError(PSTR("I2cW a=0x%02X r=0x%02X err=%d"), addr, reg, err);
    return false;
}

CAT9554::CAT9554(uint8_t sda, uint8_t scl, uint16_t frequency)
{
    // Global Arduino GPIO calls for bus recovery
    ::pinMode(sda, INPUT_PULLUP);
    ::pinMode(scl, OUTPUT);
    for (int i = 0; i < 16; i++)
    {
        ::digitalWrite(scl, LOW);
        delayMicroseconds(25);
        ::digitalWrite(scl, HIGH);
        delayMicroseconds(25);
    }
    // STOP condition
    ::pinMode(sda, OUTPUT);
    ::digitalWrite(sda, LOW);
    delayMicroseconds(25);
    ::digitalWrite(scl, HIGH);
    delayMicroseconds(25);
    ::digitalWrite(sda, HIGH);
    delayMicroseconds(25);
    ::pinMode(sda, INPUT_PULLUP);

    Wire.begin(sda, scl);
    Wire.setClock(frequency);
    Wire.setClockStretchLimit(150000L);
}

bool CAT9554::readByte(uint8_t reg, uint8_t *data)
{
    bool status = I2cValidRead(this->i2cAddress, reg, 1);
    *data = (uint8_t)i2c_buffer;
    return status;
}

bool CAT9554::writeByte(uint8_t reg, uint16_t value)
{
    return I2cWrite(this->i2cAddress, reg, value, 1);
}

bool CAT9554::readGpio()
{
    uint8_t data = 0;
    if (!this->readByte(INPUT_REG, &data))
    {
        return false;
    }
    this->inputMask = data;
    return true;
}

bool CAT9554::writeGpio()
{
    if (!this->writeByte(OUTPUT_REG, this->outputMask))
    {
        return false;
    }
    return true;
}

bool CAT9554::configGpio()
{
    bool ok = this->writeByte(CONFIG_REG, this->configMask);
    if (!ok)
    {
        this->writeByte(INPUT_REG, this->configMask);
        ok = this->writeByte(CONFIG_REG, this->configMask);
        this->writeByte(INPUT_REG, 0x00);
    }
    return ok;
}

bool CAT9554::readConfig()
{
    uint8_t data = 0;
    if (!this->readByte(CONFIG_REG, &data))
    {
        return false;
    }
    this->configMask = data;
    return true;
}

void ICACHE_RAM_ATTR CAT9554::gpioIntr()
{
    CAT9554::updateGpio = true;
}

void CAT9554::setup()
{
    bool ready = false;
    // Check 0x20
    for (uint8_t i = 0; i < 5; i++)
    {
        if (this->readGpio())
        {
            ready = true;
            Debug::AddInfo(PSTR("CAT9554 ready at 0x%02X"), this->i2cAddress);
            break;
        }
        delay(10);
        yield();
    }

    // Check 0x38 if 0x20 failed
    if (!ready)
    {
        this->i2cAddress = 0x38;
        for (uint8_t i = 0; i < 5; i++)
        {
            if (this->readGpio())
            {
                ready = true;
                Debug::AddInfo(PSTR("CAT9554 ready at 0x38"));
                break;
            }
            delay(10);
            yield();
        }
    }

    if (!ready)
    {
        this->i2cAddress = CAT9554_ADDRESS;
        Debug::AddError(PSTR("CAT9554 setup failed!"));
        return;
    }

    if (this->enableIrq)
    {
        ::pinMode(this->irqPin, INPUT_PULLUP);
        attachInterrupt(this->irqPin, gpioIntr, FALLING);
        CAT9554::updateGpio = false;
    }
    this->readGpio();
    this->readConfig();
}

bool CAT9554::digitalRead(uint8_t pin)
{
    if (!this->enableIrq || CAT9554::updateGpio)
    {
        this->readGpio();
        CAT9554::updateGpio = false;
    }
    return this->inputMask & (1 << pin);
}

bool CAT9554::digitalWrite(uint8_t pin, bool value)
{
    if (value)
    {
        this->outputMask |= (1 << pin);
    }
    else
    {
        this->outputMask &= ~(1 << pin);
    }
    return this->writeGpio();
}

void CAT9554::pinMode(uint8_t pin, uint8_t mode)
{
    if (mode == CAT9554_INPUT)
    {
        this->configMask |= (1 << pin);
    }
    else if (mode == CAT9554_OUTPUT)
    {
        this->configMask &= ~(1 << pin);
    }
    this->configGpio();
}
