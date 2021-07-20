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
 * \file ExternalTrainDbEntry.hxx
 *
 * 
 *
 * @author Balazs Racz
 * @date Jul 2 2016
 */

#ifndef _COMMANDSTATION_EXTERNALTRAINDBENTRY_HXX_
#define _COMMANDSTATION_EXTERNALTRAINDBENTRY_HXX_

#include "TrainDb.hxx"

namespace commandstation {

class ExternalTrainDbEntry : public TrainDbEntry
{
 public:
  ExternalTrainDbEntry(
    const string& name, uint16_t address, DccMode mode = DCC_128)
    : name_(name), description_(name), address_(address), mode_(mode)
  {

  }

  /// Returns an internal identifier that uniquely defines where this traindb
  /// entry was allocated from.
  string identifier() override
  {
    return "";
  }

  /// Retrieves the NMRAnet NodeID for the virtual node that represents a
  /// particular train known to the database.
  openlcb::NodeID get_traction_node() override
  {
    return 0;
  }

  /// Retrieves the name of the train.
  string get_train_name() override
  {
    return name_;
  }

  /// Sets the name of the train.
  void set_train_name(string name) override
  {
    name_ = name;
  }

  /// Retrieves the description of the train.
  string get_train_description() override
  {
    return description_;
  }

  /// Sets the description of the train.
  void set_train_description(string description) override
  {
    description_ = description;
  }

  /// Retrieves the legacy address of the train.
  uint16_t get_legacy_address() override
  {
    return address_;
  }

  /// Sets the legacy address of the train.
  void set_legacy_address(uint16_t address) override
  {
    address_ = address;
  }

  /// Retrieves the traction drive mode of the train.
  DccMode get_legacy_drive_mode() override
  {
    return mode_;
  }

  /// Retrieves the traction drive mode of the train.
  void set_legacy_drive_mode(DccMode mode) override
  {
    mode_ = mode;
  }

  /// Retrieves the label assigned to a given function, or FN_NONEXISTANT if
  /// the function does not exist.
  Symbols get_function_label(unsigned fn_id) override
  {
    return FN_NONEXISTANT;
  }

  /// Sets the label assigned to a given function.
  void set_function_label(unsigned fn_id, Symbols label) override
  {
  }

  /// Returns the largest valid function ID for this train, or -1 if the train
  /// has no functions.
  int get_max_fn() override
  {
    return 0;
  }

  /// Setup for get_max_fn().
  void start_read_functions() override
  {

  }
  
  string name_;
  string description_;
  uint16_t address_;
  DccMode mode_;
};


} // namespace commandstaiton

#endif // _COMMANDSTATION_EXTERNALTRAINDBENTRY_HXX_