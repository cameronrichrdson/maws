/*
File:   DS1881.cpp
Author: J. Ian Lindsay
Date:   2016.12.26

Copyright 2019 Manuvr, Inc

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


This class always enabled zero-cross. This induces a latency to wiper change.
Class also configures the device for maximum resolution.
*/

#include "DS1881.h"

#define DS1881_REG_WR0   0x00
#define DS1881_REG_WR1   0x40
#define DS1881_REG_CONF  0x80


const char* const DS1881::errorToStr(DIGITALPOT_ERROR err) {
  switch (err) {
    case DIGITALPOT_ERROR::DEVICE_DISABLED:  return "DEVICE_DISABLED";
    case DIGITALPOT_ERROR::PEGGED_MAX:       return "PEGGED_MAX";
    case DIGITALPOT_ERROR::PEGGED_MIN:       return "PEGGED_MIN";
    case DIGITALPOT_ERROR::NO_ERROR:         return "NO_ERROR";
    case DIGITALPOT_ERROR::ABSENT:           return "ABSENT";
    case DIGITALPOT_ERROR::BUS:              return "BUS";
    case DIGITALPOT_ERROR::ALREADY_AT_MAX:   return "ALREADY_AT_MAX";
    case DIGITALPOT_ERROR::ALREADY_AT_MIN:   return "ALREADY_AT_MIN";
    case DIGITALPOT_ERROR::INVALID_POT:      return "INVALID_POT";
    default:                                 return "UNKNOWN";
  }
}

// Everytime you macro a function, baby Jesus cries.
inline float    strict_max(float    a, float    b) {  return (a > b) ? a : b; };
inline uint32_t strict_max(uint32_t a, uint32_t b) {  return (a > b) ? a : b; };
inline uint16_t strict_max(uint16_t a, uint16_t b) {  return (a > b) ? a : b; };
inline uint8_t  strict_max(uint8_t  a, uint8_t  b) {  return (a > b) ? a : b; };
inline int32_t  strict_max(int32_t  a, int32_t  b) {  return (a > b) ? a : b; };
inline int16_t  strict_max(int16_t  a, int16_t  b) {  return (a > b) ? a : b; };
inline int8_t   strict_max(int8_t   a, int8_t   b) {  return (a > b) ? a : b; };

inline float    strict_min(float    a, float    b) {  return (a < b) ? a : b; };
inline uint32_t strict_min(uint32_t a, uint32_t b) {  return (a < b) ? a : b; };
inline uint16_t strict_min(uint16_t a, uint16_t b) {  return (a < b) ? a : b; };
inline uint8_t  strict_min(uint8_t  a, uint8_t  b) {  return (a < b) ? a : b; };
inline int32_t  strict_min(int32_t  a, int32_t  b) {  return (a < b) ? a : b; };
inline int16_t  strict_min(int16_t  a, int16_t  b) {  return (a < b) ? a : b; };
inline int8_t   strict_min(int8_t   a, int8_t   b) {  return (a < b) ? a : b; };


/*******************************************************************************
*   ___ _              ___      _ _              _      _
*  / __| |__ _ ______ | _ ) ___(_) |___ _ _ _ __| |__ _| |_ ___
* | (__| / _` (_-<_-< | _ \/ _ \ | / -_) '_| '_ \ / _` |  _/ -_)
*  \___|_\__,_/__/__/ |___/\___/_|_\___|_| | .__/_\__,_|\__\___|
*                                          |_|
* Constructors/destructors, class initialization functions and so-forth...
*******************************************************************************/

DS1881::DS1881(const uint8_t addr) : _ADDR(addr) {
}

DS1881::DS1881(const uint8_t* buf, const unsigned int len) : _ADDR(*(buf + 1)) {
  unserialize(buf, len);
}


DS1881::~DS1881() {
}


/*******************************************************************************
* Class-specific functions...                                                  *
*******************************************************************************/
/*
* Call to read the device and cause this class's state to reflect that of the device.
*/
DIGITALPOT_ERROR DS1881::init(TwoWire* b) {
  DIGITALPOT_ERROR ret = DIGITALPOT_ERROR::BUS;
  if (nullptr != b) {
    _bus = b;
  }

  if (_from_blob()) {
    // Copy the blob-imparted values and clear the flag so we don't do this again.
    _ds_clear_flag(DS1881_FLAG_FROM_BLOB);
    uint8_t vals[3] = {0, 0, 0};
    for (uint8_t i = 0; i < 3; i++) {  vals[i] = registers[i];  }
    for (uint8_t i = 0; i < 3; i++) {
      if (0 != _write_register(i << 6, vals[i])) {
        return DIGITALPOT_ERROR::BUS;
      }
    }
    _ds_set_flag(DS1881_FLAG_INITIALIZED);
    ret = DIGITALPOT_ERROR::NO_ERROR;
  }
  else if (0 == _read_registers()) {
    ret = DIGITALPOT_ERROR::NO_ERROR;
  }
  return ret;
}


DIGITALPOT_ERROR DS1881::refresh() {
  return (0 != _read_registers()) ? DIGITALPOT_ERROR::BUS : DIGITALPOT_ERROR::NO_ERROR;
}


/*
* Set the value of the given wiper to the given value.
*/
DIGITALPOT_ERROR DS1881::setValue(uint8_t pot, uint8_t val) {
  if (pot > 1)         return DIGITALPOT_ERROR::INVALID_POT;
  if (!initialized())  return DIGITALPOT_ERROR::DEVICE_DISABLED;

  DIGITALPOT_ERROR ret = (0 == val) ? DIGITALPOT_ERROR::PEGGED_MAX : DIGITALPOT_ERROR::NO_ERROR;
  uint8_t range = (uint8_t) getRange();
  uint8_t tmp_val = strict_min(val, range);
  if (range == tmp_val) {
    ret = (tmp_val == val) ? DIGITALPOT_ERROR::PEGGED_MIN : DIGITALPOT_ERROR::ALREADY_AT_MIN;
  }

  if (0 <= (int8_t) ret) {
    registers[pot] = (pot << 6) | tmp_val;
    if (0 != _write_register(pot << 6, registers[pot])) {
      ret = DIGITALPOT_ERROR::BUS;
    }
  }
  return ret;
}


/*
* Set the value of the given wiper to the given value.
*/
DIGITALPOT_ERROR DS1881::setValue(uint8_t val) {
  DIGITALPOT_ERROR return_value = setValue(0, val);
  if (0 <= (int8_t) return_value) {
    return_value = setValue(1, val);
  }
  return return_value;
}


/*
* Enabling the device unmutes it by restoring the previous value from the alt_values array.
* Disabling the device stacks the current value into alt_values and then sets to mute.
* Retains wiper settings.
*/
DIGITALPOT_ERROR DS1881::enable(bool x) {
  DIGITALPOT_ERROR return_value = DIGITALPOT_ERROR::NO_ERROR;
  if (x ^ enabled()) {
    if (x) {
      _write_register(DS1881_REG_WR0, alt_values[0]);
      _write_register(DS1881_REG_WR1, alt_values[1]);
    }
    else {
      uint8_t range = (uint8_t) getRange();
      alt_values[0] = registers[0];
      alt_values[1] = registers[1];
      _write_register(DS1881_REG_WR0, range);
      _write_register(DS1881_REG_WR1, range);
    }
    _ds_set_flag(DS1881_FLAG_ENABLED, x);
  }
  return return_value;
}


/*
* Make this state non-volatile.
*/
DIGITALPOT_ERROR DS1881::storeWipers() {
  DIGITALPOT_ERROR return_value = DIGITALPOT_ERROR::BUS;
  // Temporarily change the wiper volatility bit to write to the NVM...
  if (0 == _write_register(DS1881_REG_CONF, (registers[2] & 0x07) & ~0x04)) {
    // Store the wiper(s).
    if (0 == _write_register(DS1881_REG_WR0, registers[0])) {
      if (0 == _write_register(DS1881_REG_WR1, registers[1])) {
        // Set the wipers back to volatile.
        //if (0 == _write_register(DS1881_REG_CONF, (registers[2] & 0x07) | 0x04)) {
          return_value = DIGITALPOT_ERROR::NO_ERROR;
        //}
      }
    }
  }

  return return_value;
}


DIGITALPOT_ERROR DS1881::zerocrossWait(bool en) {
  int8_t return_value = _write_register(DS1881_REG_CONF, (en ? (registers[2] | 0x02) : (registers[2] & ~0x02)));
  return (0 == return_value) ? DIGITALPOT_ERROR::NO_ERROR : DIGITALPOT_ERROR::BUS;
}


DIGITALPOT_ERROR DS1881::setRange(uint8_t val) {
  int8_t return_value = -1;
  switch (val) {
    case 33:
    case 63:
      return_value = _write_register(DS1881_REG_CONF, ((33 == val) ? (registers[2] | 0x01) : (registers[2] & ~0x01)));
    default:
      break;
  }
  return (0 == return_value) ? DIGITALPOT_ERROR::NO_ERROR : DIGITALPOT_ERROR::BUS;
}



int8_t DS1881::_write_register(uint8_t reg, uint8_t val) {
  int8_t ret = -1;
  if (nullptr != _bus) {
    if (reg == (reg & 0xC0)) {
      _bus->beginTransmission(_ADDR);
      _bus->write((0x3F & val) | reg);
      if (0 == _bus->endTransmission()) {
        registers[reg >> 6] = val;
        ret = 0;
      }
    }
  }
  return ret;
}


int8_t DS1881::_read_registers() {
  int8_t ret = -1;
  if (nullptr != _bus) {
    if (3 == _bus->requestFrom(_ADDR, (uint8_t) 3)) {
      ret = 0;
      registers[0] = 0x3F & _bus->receive();
      registers[1] = 0x3F & _bus->receive();
      registers[2] = 0x3F & _bus->receive();
    }

    if (0x06 != (registers[2] & 0x07)) {
      // Enforces high-res volatile wiper operation unless specified otherwise.
      ret = _write_register(DS1881_REG_CONF, (registers[2] & 0xFE) | 0x06);
    }
    _ds_set_flag(DS1881_FLAG_INITIALIZED, (0 == ret));
  }
  return ret;
}


/*
* Stores everything about the class in the provided buffer in this format...
*   Offset | Data
*   -------|----------------------
*   0      | Serializer version
*   1      | i2c address
*   2      | Flags
*   3      | Wiper0 register
*   4      | Wiper1 register
*   5      | Conf register
*
* Returns the number of bytes written to the buffer.
*/
uint8_t DS1881::serialize(uint8_t* buf, unsigned int len) {
  uint8_t offset = 0;
  if (len >= DS1881_SERIALIZE_SIZE) {
    if (initialized()) {
      *(buf + offset++) = DS1881_SERIALIZE_VERSION;
      *(buf + offset++) = _ADDR;
      *(buf + offset++) = _flags & DS1881_FLAG_SERIAL_MASK;
      for (uint8_t i = 0; i < 3; i++) {
        *(buf + offset++) = registers[i];
      }
    }
  }
  return offset;
}


int8_t DS1881::unserialize(const uint8_t* buf, const unsigned int len) {
  uint8_t offset = 0;
  uint8_t expected_sz = 255;
  if (len >= DS1881_SERIALIZE_SIZE) {
    uint8_t vals[3] = {0, 0, 0};
    switch (*(buf + offset++)) {
      case DS1881_SERIALIZE_VERSION:
        expected_sz = DS1881_SERIALIZE_SIZE;
        offset += 1;  // We'll have already constructed with _ADDR.
        _flags = (_flags & ~DS1881_FLAG_SERIAL_MASK) | (*(buf + offset++) & DS1881_FLAG_SERIAL_MASK);
        vals[0] = *(buf + offset++);
        vals[1] = *(buf + offset++);
        vals[2] = *(buf + offset++);
        break;
      default:  // Unhandled serializer version.
        return -1;
    }
    if (_ds_flag(DS1881_FLAG_INITIALIZED)) {
      // If the device has already been initialized, we impart the new conf.
      for (uint8_t i = 0; i < 3; i++) {
        if (0 != _write_register(i << 6, vals[i])) {
          return -2;
        }
      }
    }
    else {
      _ds_set_flag(DS1881_FLAG_FROM_BLOB);
      for (uint8_t i = 0; i < 3; i++) {
        registers[i] = vals[i];   // Save state for init()
      }
    }
  }
  return (expected_sz == offset) ? 0 : -1;
}


#if defined(DS1881_DEBUG)
/*
* Dump this item to the given buffer.
*/
void DS1881::printDebug(StringBuilder* output) {
  output->concatf("DS1881 digital potentiometer\n");
  output->concatf("\tInitialized:    %c\n", initialized() ? 'y' : 'n');
  if (initialized()) {
    output->concatf("\tCONF:           0x%02x\n", registers[2]);
    output->concatf("\tRange:          %u\n", getRange());
    output->concatf("\tZero-cross:     %c\n", zerocrossWait() ? 'y' : 'n');
    for (int i = 0; i < 2; i++) {
      output->concatf("\tPOT %u:  %u  (Alt: %u)\n", i, 0x3F & registers[i], alt_values[i]);
    }
  }
}

#endif  // DS1881_DEBUG
