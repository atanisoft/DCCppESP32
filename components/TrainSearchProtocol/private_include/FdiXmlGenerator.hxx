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
 * \file FdiXmlGenerator.hxx
 *
 * Train FDI generator.
 *
 * @author Balazs Racz
 * @date 16 Jan 2016
 */

#include "TrainDb.hxx"
#include "XmlGenerator.hxx"

namespace commandstation {

class FdiXmlGenerator : public XmlGenerator {
 public:
  /// Call this after the lokdb on entry was overwritten with the new loco's
  /// data.
  void reset(std::shared_ptr<TrainDbEntry> entry);

 private:
  void generate_more() override;

  enum State {
    STATE_START = 0,
    STATE_XMLHEAD = STATE_START,
    STATE_START_FN,
    STATE_FN_NAME,
    STATE_FN_NUMBER,
    STATE_FN_END,
    STATE_NO_MORE_FN,
    STATE_EOF
  };

  State state_;
  std::shared_ptr<TrainDbEntry> entry_;
  int nextFunction_;
};

}  // namespace commandstation