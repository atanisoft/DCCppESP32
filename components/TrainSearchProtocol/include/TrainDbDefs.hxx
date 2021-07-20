/** \copyright
 * Copyright (c) 2016, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file TrainDbDefs.hxx
 *
 * Common definitions for the train database.
 *
 * @author Balazs Racz
 * @date 13 Feb 2016
 */

#ifndef _COMMANDSTATION_TRAINDBDEFS_HXX_
#define _COMMANDSTATION_TRAINDBDEFS_HXX_

#include <utils/logging.h>
#include <dcc/Defs.hxx>

namespace commandstation
{

static constexpr size_t DCC_MAX_FN = 29;

enum Symbols
{
  FN_NONEXISTANT = 0,
  LIGHT = 1,
  BELL = 2,
  HORN = 128 + 3,
  WHISTLE = 128 + 4,
  SHUNT = 5,
  MOMENTUM = 6,
  SMOKE = 7,
  MUTE = 8,
  GENERIC = 9,
  COUPLER = 10,

  FN_UNKNOWN = 127,
  MOMENTARY = 128,
  // Empty eeprom will have these bytes.
  FN_UNINITIALIZED = 255,
};

// Use cases we need to support:
//
// - search, any protocol
// - search, specific address mode (dcc-short, dcc-long, marklin, dead rail)
// - allocate, any protocol
// - allocate, specific address mode (dcc-short, dcc-long, marklin, dead rail)
// - allocate, specific address mode and speed steps
//
// Dead rail is an address mode.
// Marklin old, new, twoaddr is a speed step mode
// DCC 14, 28, 128 is a speed step mode
// MFX is an address mode but unclear what the address is actually
//
// DCC protocol options: 3 bits: short/long; default-14-28-128.
// Marklin protocol options: old-new-twoaddr. Mfx?
// base protocol: dcc, marklin, deadrail
// dcc options: 3 bits
// marklin options: 2 bits (f0, f4, f8, mfx)
// additional protocols: selectrix, mth?,
// what if we don't want to express a preference?
//   most settings have to have a neutral choice.
//   eg. dcc default; dcc force long
//   dcc default-14-28-128
//   marklin default could be marklin new
// 
// Search protocol reserves bits 0x80, 0x40, 0x20
// This leaves 5 bits for protocol. Protocol base = 2 bits; protocol options = 3 bits.
// protocol base: bits 0,1:
//    . 0x0 : default / unspecified, olcbuser, all marklin fits here.
//    . 0x1 : dcc
//    . 0x2, 0x3 : reserved (expansion)
//
// marklin option bits: 2 bits should be default, force f0, force f8, force mfx
// third option bit should be reserved, check as zero
//
// default protocol needs to have the expansion bits reserved, check as zero.
//
// can we fold default and marklin over each other?
// two bits: 00
// force long == 1 => marklin
// 00 0 00: default
// 00 0 01: olcb direct
// 00 0 10: mfx ?
//
// 00 1 00: marklin default
// 00 1 01: marklin old
// 00 1 10: marklin new
// 00 1 11: marklin 2-address
//
// 01 0 00: dcc (default)
// 01 1 00: dcc long
// 01 0 01: dcc 14
// 01 1 01: dcc 14 long does not exist (all long address capable dcc decoders support 28)
// 01 0 10: dcc 28
// 01 1 10: dcc 28 long
// 01 0 11: dcc 128
// 01 1 11: dcc 128 long



enum DccMode
{
  DCCMODE_DEFAULT = 0,
  DCCMODE_FAKE_DRIVE = 1,
  DCCMODE_OLCBUSER = 1,

  /// Value for testing whether the protocol is a Markin-Motorola protocol
  /// variant.
  MARKLIN_ANY = 0b00100,
  /// Mask for testing whether the protocol is a Markin-Motorola protocol
  /// variant.
  MARKLIN_ANY_MASK = 0b11100,
  /// Mask for the Marklin protocol version speed step setting.
  MARKLIN_V_MASK = 0b00011,

  /// Acquisition for a Marklin locomotive with default setting.
  MARKLIN_DEFAULT = MARKLIN_ANY,
  /// Force MM protocol version 1 (F0 only).
  MARKLIN_OLD = MARKLIN_ANY | 1,
  /// Force MM protocol version 2 (F0-F4).
  MARKLIN_NEW = MARKLIN_ANY | 2,
  /// Force MM protocol version 2 with subsequent address for more functions
  /// (F0-F8).
  MARKLIN_TWOADDR = MARKLIN_ANY | 3,
  /// Alias for MFX locmotives (to be driven with Marklin v2 protocol for now).
  MFX = MARKLIN_NEW,

  /// value for testing whether the protocol is a DCC variant
  DCC_ANY = 0b01000,
  /// mask for testing whether the protocol is a DCC variant
  DCC_ANY_MASK = 0b11000,

  /// Acquisition for DCC locomotive with default settings.
  DCC_DEFAULT = DCC_ANY,
  /// Force long address for DCC. If clear, uses default address type by number.
  DCC_LONG_ADDRESS = 0b00100,
  /// Force long address for DCC with DEFAULT/ANY speed step mode.
  DCC_DEFAULT_LONG_ADDRESS = DCC_DEFAULT | DCC_LONG_ADDRESS,
  /// Mask for the DCC speed step setting.
  DCC_SS_MASK = 0b00011,
  /// Unpecified / default speed step setting.
  DCC_DEFAULT_SS = DCC_DEFAULT,
  /// Force 14 SS mode
  DCC_14 = DCC_ANY | 1,
  /// Force 28 SS mode
  DCC_28 = DCC_ANY | 2,
  /// Force 128 SS mode
  DCC_128 = DCC_ANY | 3,
  /// Force 14 SS mode & long address (this is meaningless).
  DCC_14_LONG_ADDRESS = DCC_14 | DCC_LONG_ADDRESS,
  /// Force 28 SS mode & long address.
  DCC_28_LONG_ADDRESS = DCC_28 | DCC_LONG_ADDRESS,
  /// Force 128 SS mode & long address.
  DCC_128_LONG_ADDRESS = DCC_128 | DCC_LONG_ADDRESS,

  /// Bit mask for the protocol field only.
  DCCMODE_PROTOCOL_MASK = 0b11111,
};

/// Converts a DccMode bit mask and a legacy address into a TrainAddressType
/// enum.
/// @param mode the legacy drive mode (e.g. from a TrainDb entry or from a search query)
/// @param address is the legacy address.
/// @return an enum value which together with the address uniquely represents
/// an addressable entity on the track. May return UNSPECIFIED if DccMode ==
/// DCCMODE_DEFAULT (usually a query did not specify any restriction) or
/// UNSUPPORTED if we did not recognize the code in the DccMode bitfield.
inline dcc::TrainAddressType dcc_mode_to_address_type(DccMode mode,
                                                      uint32_t address)
{
  if (mode == DCCMODE_DEFAULT)
  {
    return dcc::TrainAddressType::UNSPECIFIED;
  }
  if ((mode & MARKLIN_ANY_MASK) == MARKLIN_ANY)
  {
    return dcc::TrainAddressType::MM;
  }
  if ((mode & DCC_ANY_MASK) == DCC_ANY)
  {
    if ((mode & DCC_LONG_ADDRESS) || (address >= 128))
    {
      return dcc::TrainAddressType::DCC_LONG_ADDRESS;
    }
    return dcc::TrainAddressType::DCC_SHORT_ADDRESS;
  }
  LOG_ERROR("Unsupported drive mode %d (0x%02x)", mode, mode);
  return dcc::TrainAddressType::UNSUPPORTED;
}

/// Converts a DccMode bit mask down to a protocol enumeration, i.e. DCC,
/// Marklin or OpenLCB.
/// @param mode the detailed mode bit field.
/// @return a stripped down mode bit field which does not specify any details
/// about the protocol variant.
inline DccMode dcc_mode_to_protocol(DccMode mode)
{
  if (mode == DCCMODE_DEFAULT ||
      mode == DCCMODE_OLCBUSER)
  {
    return mode;
  }
  if ((mode & MARKLIN_ANY_MASK) == MARKLIN_ANY)
  {
    return MARKLIN_ANY;
  }
  if ((mode & DCC_ANY_MASK) == DCC_ANY)
  {
    return DCC_ANY;
  }
  LOG_ERROR("Unknown DCC Mode %d", (int)mode);
  // We return the value unchanged.
  return mode;
}

} // namespace commandstation

#endif // _COMMANDSTATION_TRAINDBDEFS_HXX_