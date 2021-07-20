/** \copyright
 * Copyright (c) 2014-2016, Balazs Racz
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
 * \file FindProtocolDefs.hxx
 *
 * Definitions for the train node find protocol.
 *
 * @author Balazs Racz
 * @date 18 Feb 2016
 */

#ifndef _COMMANDSTATION_FINDPROTOCOLDEFS_HXX_
#define _COMMANDSTATION_FINDPROTOCOLDEFS_HXX_

#include <openlcb/EventHandler.hxx>
#include "TrainDbDefs.hxx"

namespace commandstation {

class TrainDbEntry;

struct FindProtocolDefs {
  // static constexpr EventID
  enum {
    TRAIN_FIND_BASE = 0x090099FF00000000U,
  };

  // Command byte definitions
  enum {
    // What is the mask value for the event registry entry.
    TRAIN_FIND_MASK = 32,
    // Where does the command byte start.
    TRAIN_FIND_MASK_LOW = 8,

    ALLOCATE = 0x80,
    // SEARCH = 0x00,

    EXACT = 0x40,
    // SUBSTRING = 0x00,

    ADDRESS_ONLY = 0x20,
    // ADDRESS_NAME_CABNUMBER = 0x00

    // Bits 0-4 are a DccMode enum.
    
    // Match response information. This bit is not used in the network
    // protocol. The bit will be set in the matched result, while cleared for a
    // no-match.
    MATCH_ANY = 0x01,
  };

  static_assert((TRAIN_FIND_BASE & ((1ULL << TRAIN_FIND_MASK) - 1)) == 0,
                "TRAIN_FIND_BASE is not all zero on the bottom");

  // Search nibble definitions.
  enum {
    NIBBLE_UNUSED = 0xf,
    NIBBLE_SPACE = 0xe,
    NIBBLE_STAR = 0xd,
    NIBBLE_QN = 0xc,
    NIBBLE_HASH = 0xb,
  };

  /// @param event is an openlcb event ID
  /// @return true if that event ID belong to the find protocol event range.
  static bool is_find_event(openlcb::EventId event) {
    return (event >> TRAIN_FIND_MASK) == (TRAIN_FIND_BASE >> TRAIN_FIND_MASK);
  }

  /// Compares an incoming search query's drive mode bits to an actual drive
  /// mode of a locomotive. Decides whether they match using tri-state logic,
  /// i.e. taking into account "no restriction" queries.
  /// @param event the incoming query
  /// @param mode the drive mode of a locomotive
  /// @return true if this locomotive matches the restrictions in the query
  /// (true if there were no restrictions in the query).
  static bool match_event_to_drive_mode(openlcb::EventId event, DccMode mode);

  /** Compares an incoming search query to a given train node. Returns 0 for a
      no-match. Returns a bitfield of match types for a match. valid bits are
      MATCH_ANY (always set), ADDRESS_ONLY (set when the match occurred in the
      address), EXACT (clear for prefix match). */
  static uint8_t match_query_to_node(openlcb::EventId event, TrainDbEntry* train);

  /** Compares an incoming search query to a train node described by the major
   * parameters only. mode should be set to 0 for ignore, or DCC_LONG_ADDRESS.
   * Returns a bitfield of match types for a match. valid bits are MATCH_ANY
   * (always set), ADDRESS_ONLY (set when the match occurred in the address),
   * EXACT (clear for prefix match).
   */
  static uint8_t match_query_to_train(openlcb::EventId event,
                                      const string& name, unsigned address,
                                      DccMode mode);

  /** Converts a find protocol query to an address and desired DccMode
      information. Will take into account prefix zeros for forcing a dcc long
      address, as well as all mode and flag bits coming in via the query.
      
      @param mode (can't be null) will be filled in with the Dcc Mode: the
      bottom 3 bits as specified by the incoming query, or zero if the query
      did not specify a preference. If the query started with a prefix of zero
      (typed by the user) or DCC_FORCE_LONG_ADDRESS was set in the query, the
      DccMode will have the force long address bit set.

      @returns the new legacy_address. */
  static unsigned query_to_address(openlcb::EventId query, DccMode* mode);

  /** Translates an address as punched in by a (dumb) throttle to a query to
   * issue on the OpenLCB bus as a find protocol request.
   *
   * @param address is the numeric value that the user typed.
   * @param exact should be true if only exact matches shall be retrieved.
   * @param mode should be set most of the time to OLCBUSER to specify that we
   * don't care about the address type, but can also be set to
   * DCC_LONG_ADDRESS.
   */
  static openlcb::EventId address_to_query(unsigned address, bool exact, DccMode mode);

  /** Translates an address as punched in by a (dumb) throttle to a query to
   * issue on the OpenLCB bus as a find protocol request. This method ensures
   * that the allocate bit is set so that if the address (node) does not yet
   * exist, it may be created.
   *
   * @param address is the numeric value that the user typed.
   * @param exact should be true if only exact matches shall be retrieved.
   * @param mode should be set most of the time to OLCBUSER to specify that we
   * don't care about the address type, but can also be set to
   * DCC_LONG_ADDRESS.
   */
  static openlcb::EventId address_to_allocate(unsigned address, bool exact, DccMode mode)
  {
    return address_to_query(address, exact, mode) | ALLOCATE;
  }

  /** Translates a sequence of input digits punched in by a throttle to a query
   * to issue on the OpenLCB bus as a find protocol request.
   *
   * @param input is the sequence of numbers that the user typed. This is
   * expected to have form like '415' or '021' or '474014'
   * @return an event ID representing the search. This event ID could be
   * IS_TRAIN_EVENT.
   */
  static openlcb::EventId input_to_search(const string& input);

  /** Translates a sequence of input digits punched in by a throttle to an
   * allocate request to issue on the OpenLCB bus.
   *
   * @param input is the sequence of numbers that the user typed. This is
   * expected to have form like '415' or '021' or '474014'. You can add a
   * leading zero to force DCC long address, a trailing M to force a Marklin
   * locomotive.
   * @return an event ID representing the search. This event ID will be zero if
   * the user input is invalid.
   */
  static openlcb::EventId input_to_allocate(const string& input);

  /// Specifies what kind of train to allocate when the drive mode is left as
  /// default / unspecified.
  static uint8_t DEFAULT_DRIVE_MODE;

  /// Specifies what kind of train to allocate when the drive mode is set as
  /// MARKLIN_ANY.
  static uint8_t DEFAULT_MARKLIN_DRIVE_MODE;

  /// Specifies what kind of train to allocate when the drive mode is set as
  /// DCC_ANY.
  static uint8_t DEFAULT_DCC_DRIVE_MODE;

 private:
  /// Helper function for the input_to_* calls.
  static openlcb::EventId input_to_event(const string& input);
  
  // Not instantiatable class.
  FindProtocolDefs();
};

}  // namespace commandstation

#endif  // _COMMANDSTATION_FINDPROTOCOLDEFS_HXX_