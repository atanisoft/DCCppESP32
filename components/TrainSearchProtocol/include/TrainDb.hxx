/** \copyright
 * Copyright (c) 2014, Balazs Racz
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
 * \file TrainDb.hxx
 *
 * Interface for accessing a train database for the mobile station lookup.
 *
 * @author Balazs Racz
 * @date 18 May 2014
 */

#ifndef _MOBILESTATION_TRAINDB_HXX_
#define _MOBILESTATION_TRAINDB_HXX_

#include <memory>
#include <openlcb/Defs.hxx>
#include <utils/ConfigUpdateListener.hxx>
#include "TrainDbDefs.hxx"
#include "TrainDbCdi.hxx"

namespace commandstation
{

class TrainDbEntry
{
public:
  virtual ~TrainDbEntry() {}

  /// Returns an internal identifier that uniquely defines where this traindb
  /// entry was allocated from.
  virtual string identifier() = 0;

  /// Retrieves the NMRAnet NodeID for the virtual node that represents a
  /// particular train known to the database.
  virtual openlcb::NodeID get_traction_node() = 0;

  /// Retrieves the name of the train.
  virtual string get_train_name() = 0;

  /// Sets the name of the train.
  virtual void set_train_name(string name) = 0;

  /// Retrieves the description of the train.
  virtual string get_train_description() = 0;

  /// Sets the description of the train.
  virtual void set_train_description(string description) = 0;

  /// Retrieves the legacy address of the train.
  virtual uint16_t get_legacy_address() = 0;

  /// Sets the legacy address of the train.
  virtual void set_legacy_address(uint16_t address) = 0;

  /// Retrieves the traction drive mode of the train.
  virtual DccMode get_legacy_drive_mode() = 0;

  /// Sets the traction drive mode of the train.
  virtual void set_legacy_drive_mode(DccMode mode) = 0;

  /// Retrieves the label assigned to a given function, or FN_NONEXISTANT if
  /// the function does not exist.
  virtual Symbols get_function_label(unsigned fn_id) = 0;

  /// Sets the label assigned to a given function.
  virtual void set_function_label(unsigned fn_id, Symbols label) = 0;

  /// Returns the largest valid function ID for this train, or -1 if the train
  /// has no functions.
  virtual int get_max_fn() = 0;

  /// If non-negative, represents a file offset in the openlcb CONFIG_FILENAME
  /// file where this train has its data stored.
  virtual int file_offset() { return -1; }

  /// Notifies that we are going to read all functions. Sometimes a
  /// re-initialization is helpful at this point.
  virtual void start_read_functions() = 0;
};

class TrainDb
{
 public:
  /// @returns the number of traindb entries. The valid train IDs will then be
  /// 0 <= id < size().
  virtual size_t size() = 0;

  /// Returns true if a train of a specific identifier is known to the traindb.
  /// @param train_id is the train identifier. Valid values: anything.
  /// Typical values: 0..NUM_TRAINS
  virtual bool is_train_id_known(unsigned train_id) = 0;

  /// Returns true if a train of a specific identifier is known to the traindb.
  //  @param train_id is the node id of the train being queried.
  virtual bool is_train_id_known(openlcb::NodeID train_id) = 0;

  /// Returns a train DB entry if the train ID is known, otherwise nullptr. The
  /// ownership of the entry is not transferred.
  virtual std::shared_ptr<TrainDbEntry> get_entry(unsigned train_id) = 0;

  /// Searches for an entry by the traction node ID. Returns nullptr if not
  /// found. @param hint is a train_id that might be a match. */
  virtual std::shared_ptr<TrainDbEntry> find_entry(openlcb::NodeID traction_node_id,
                                                   unsigned hint = 0) = 0;

  /// Inserts a given entry into the train database.
  /// @param address the locomotive address to create.
  /// @param mode the operating mode for the new locomotive.
  /// @returns the new train_id for the given entry.
  virtual unsigned add_dynamic_entry(uint16_t address, DccMode mode) = 0;
};

}  // namespace commandstation

#endif // _MOBILESTATION_TRAINDB_HXX_