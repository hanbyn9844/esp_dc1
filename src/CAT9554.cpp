#include "CAT9554.h"
#include "Debug.h"

bool CAT9554::updateGpio = false;

void ICACHE_RAM_ATTR CAT9554::gpioIntr()
{
    CAT9554::updateGpio = true;
}

CAT9554::CAT9554(uint8_t sda, uint8_t scl, uint16_t frequency)
{
    this->sdaPin = sda;
    this->sclPin = scl;
    if (frequency > 0 && frequency <= 50000)
    {
        this->halfPeriodUs = 500000UL / frequency;
    }
    else
    {
        this->halfPeriodUs = 25; // default 20kHz
    }
    this->i2cAddress = CAT9554_ADDRESS;
    this->inputMask = 0xFF; // All unpressed by default
    this->outputMask = 0x00;
    this->configMask = 0x0F;
    this->lastPollTime = 0;

    sdaHigh();
    sclHigh();
}

void CAT9554::busRecovery()
{
    sdaHigh();
    sclHigh();
    delayMicroseconds(this->halfPeriodUs);
    if (!sdaRead())
    {
        for (uint8_t i = 0; i < 18; i++)
        {
            sclLow();
            delayMicroseconds(this->halfPeriodUs);
            sclHigh();
            delayMicroseconds(this->halfPeriodUs);
            if (sdaRead())
            {
                break;
            }
        }
    }
    i2cStop();
}

void CAT9554::i2cStart()
{
    sdaHigh();
    sclHigh();
    delayMicroseconds(this->halfPeriodUs);
    sdaLow();
    delayMicroseconds(this->halfPeriodUs);
    sclLow();
    delayMicroseconds(this->halfPeriodUs);
}

void CAT9554::i2cStop()
{
    sdaLow();
    delayMicroseconds(this->halfPeriodUs);
    sclHigh();
    delayMicroseconds(this->halfPeriodUs);
    sdaHigh();
    delayMicroseconds(this->halfPeriodUs);
}

bool CAT9554::i2cWriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            sdaHigh();
        }
        else
        {
            sdaLow();
        }
        delayMicroseconds(this->halfPeriodUs);
        sclHigh();
        delayMicroseconds(this->halfPeriodUs);
        sclLow();
        delayMicroseconds(2);
        data <<= 1;
    }

    // Read ACK/NACK
    sdaHigh();
    delayMicroseconds(this->halfPeriodUs);
    sclHigh();
    delayMicroseconds(this->halfPeriodUs);
    bool ack = !sdaRead(); // 0 is ACK
    sclLow();
    delayMicroseconds(this->halfPeriodUs);
    return ack;
}

uint8_t CAT9554::i2cReadByte(bool ack)
{
    uint8_t data = 0;
    sdaHigh();
    for (uint8_t i = 0; i < 8; i++)
    {
        delayMicroseconds(this->halfPeriodUs);
        sclHigh();
        delayMicroseconds(this->halfPeriodUs);
        data = (data << 1) | (sdaRead() ? 1 : 0);
        sclLow();
        delayMicroseconds(2);
    }

    // Send ACK or NACK
    if (ack)
    {
        sdaLow();
    }
    else
    {
        sdaHigh();
    }
    delayMicroseconds(this->halfPeriodUs);
    sclHigh();
    delayMicroseconds(this->halfPeriodUs);
    sclLow();
    sdaHigh();
    delayMicroseconds(this->halfPeriodUs);
    return data;
}

bool CAT9554::readByte(uint8_t reg, uint8_t *data)
{
    for (uint8_t retry = 0; retry < I2C_RETRY_COUNTER; retry++)
    {
        if (retry > 0)
        {
            busRecovery();
            delay(2);
        }
        i2cStart();
        if (!i2cWriteByte((this->i2cAddress << 1) | 0))
        {
            i2cStop();
            continue;
        }
        if (!i2cWriteByte(reg))
        {
            i2cStop();
            continue;
        }
        // Repeated start
        i2cStart();
        if (!i2cWriteByte((this->i2cAddress << 1) | 1))
        {
            i2cStop();
            continue;
        }
        *data = i2cReadByte(false); // NACK on 1-byte read
        i2cStop();
        return true;
    }
    Debug::AddError(PSTR("I2cR a=0x%02X r=0x%02X err"), this->i2cAddress, reg);
    return false;
}

bool CAT9554::writeByte(uint8_t reg, uint16_t value)
{
    for (uint8_t retry = 0; retry < I2C_RETRY_COUNTER; retry++)
    {
        if (retry > 0)
        {
            busRecovery();
            delay(2);
        }
        i2cStart();
        if (!i2cWriteByte((this->i2cAddress << 1) | 0))
        {
            i2cStop();
            continue;
        }
        if (!i2cWriteByte(reg))
        {
            i2cStop();
            continue;
        }
        if (!i2cWriteByte((uint8_t)(value & 0xFF)))
        {
            i2cStop();
            continue;
        }
        i2cStop();
        return true;
    }
    Debug::AddError(PSTR("I2cW a=0x%02X r=0x%02X err"), this->i2cAddress, reg);
    return false;
}

bool CAT9554::readGpio()
{
    uint8_t data = 0xFF;
    if (!this->readByte(INPUT_REG, &data))
    {
        return false;
    }
    this->inputMask = data;
    return true;
}

bool CAT9554::writeGpio()
{
    return this->writeByte(OUTPUT_REG, this->outputMask);
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

void CAT9554::setup()
{
    busRecovery();

    bool ready = false;
    uint8_t testVal = 0;

    // Probe 0x20
    this->i2cAddress = 0x20;
    for (uint8_t i = 0; i < 5; i++)
    {
        if (this->readByte(INPUT_REG, &testVal))
        {
            ready = true;
            this->inputMask = testVal;
            Debug::AddInfo(PSTR("CAT9554 ready at 0x20 (in=0x%02X)"), testVal);
            break;
        }
        delay(10);
    }

    if (!ready)
    {
        // Probe 0x38
        this->i2cAddress = 0x38;
        for (uint8_t i = 0; i < 5; i++)
        {
            if (this->readByte(INPUT_REG, &testVal))
            {
                ready = true;
                this->inputMask = testVal;
                Debug::AddInfo(PSTR("CAT9554 ready at 0x38 (in=0x%02X)"), testVal);
                break;
            }
            delay(10);
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
    unsigned long now = millis();
    if (!this->enableIrq || CAT9554::updateGpio || (now - this->lastPollTime > 50))
    {
        if (this->readGpio())
        {
            this->lastPollTime = now;
        }
        CAT9554::updateGpio = false;
    }
    return (this->inputMask & (1 << pin)) != 0;
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
