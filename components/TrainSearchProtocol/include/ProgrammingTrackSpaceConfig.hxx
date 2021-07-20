/** \copyright
 * Copyright (c) 2018, Balazs Racz
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are strictly prohibited without written consent.
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
 * \file ProgrammingTrackSpaceConfig.hxx
 *
 * CDI configuration for the CV space to access the programming track flow.
 *
 * @author Balazs Racz
 * @date 2 June 2018
 */

#ifndef _COMMANDSTATION_PROGRAMMINGTRACKSPACECONFIG_HXX_
#define _COMMANDSTATION_PROGRAMMINGTRACKSPACECONFIG_HXX_

#include <openlcb/ConfigRepresentation.hxx>
#include <openlcb/MemoryConfig.hxx>

namespace commandstation {

static const char OPERATING_MODE_MAP_VALUES[] = R"(
<relation><property>0</property><value>Disabled</value></relation>
<relation><property>1</property><value>Direct mode</value></relation>
<relation><property>2</property><value>POM mode</value></relation>
<relation><property>10</property><value>Advanced mode</value></relation>
)";


CDI_GROUP(ProgrammingTrackSpaceConfigAdvanced);
CDI_GROUP_ENTRY(
    repeat_verify, openlcb::Uint32ConfigEntry,
    Name("Repeat count for verify packets"),
    Description("How many times a direct mode bit verify packet needs to be "
                "repeated for an acknowledgement to be generated."),
    Default(3), Min(0), Max(255));
CDI_GROUP_ENTRY(
    repeat_cooldown_reset, openlcb::Uint32ConfigEntry,
    Name("Repeat count for reset packets after verify"),
    Description("How many reset packets to send after a verify."),
    Default(6), Min(0), Max(255));
CDI_GROUP_END();

CDI_GROUP(ProgrammingTrackSpaceConfig, Segment(openlcb::MemoryConfigDefs::SPACE_DCC_CV), Offset(0x7F100000),
          Name("Programming track operation"),
          Description("Use this component to read and write CVs on the "
                      "programming track of the command station."));

enum OperatingMode {
  PROG_DISABLED = 0,
  DIRECT_MODE = 1,
  POM_MODE = 2,
  ADVANCED = 10,
};

CDI_GROUP_ENTRY(mode, openlcb::Uint32ConfigEntry, Name("Operating mode"), MapValues(OPERATING_MODE_MAP_VALUES));
CDI_GROUP_ENTRY(cv, openlcb::Uint32ConfigEntry, Name("CV number"), Description("Number of CV to read or write (1..1024)."), Default(0), Min(0), Max(1024));
CDI_GROUP_ENTRY(
    value, openlcb::Uint32ConfigEntry, Name("CV value"),
    Description(
        "Set 'Operating mode' and 'CV number' first, then: hit 'Refresh' to "
        "read the entire CV, or enter a value and hit 'Write' to set the CV."),
    Default(0), Min(0), Max(255));
CDI_GROUP_ENTRY(
    bit_write_value, openlcb::Uint32ConfigEntry, Name("Bit change"),
    Description(
        "Set 'Operating mode' and 'CV number' first, then: write 1064 to set "
        "the single bit whose value is 64, or 2064 to clear that bit. Write "
        "100 to 107 to set bit index 0 to 7, or 200 to 207 to clear bit 0 to "
        "7. Values outside of these two ranges do nothing."),
    Default(1000), Min(100), Max(2128));
CDI_GROUP_ENTRY(bit_value_string, openlcb::StringConfigEntry<24>,
                Name("Read bits decomposition"),
                Description("Hit Refresh on this line after reading a CV value "
                            "to see which bits are set."));
CDI_GROUP_ENTRY(advanced, ProgrammingTrackSpaceConfigAdvanced,
                Name("Advanced settings"));
struct Shadow;
CDI_GROUP_END();

/// This shadow structure is declared to be parallel to the CDI entries.
struct ProgrammingTrackSpaceConfig::Shadow {
  uint32_t mode;
  uint32_t cv;
  uint32_t value;
  uint32_t bit_write_value;
  char bit_value_string[24];
  uint32_t verify_repeats;
  uint32_t verify_cooldown_repeats;
};

#if __GNUC__ > 6
#define SHADOW_OFFSETOF(entry)                                  \
    (offsetof(ProgrammingTrackSpaceConfig::Shadow, entry))
#else
#define SHADOW_OFFSETOF(entry)                                          \
    ((uintptr_t) & ((ProgrammingTrackSpaceConfig::Shadow*)nullptr)->entry)
#endif

static_assert(SHADOW_OFFSETOF(cv) ==
                  ProgrammingTrackSpaceConfig::zero_offset_this().cv().offset(),
              "Offset of CV field does not match.");

static_assert(SHADOW_OFFSETOF(verify_cooldown_repeats) ==
                  ProgrammingTrackSpaceConfig::zero_offset_this()
                      .advanced()
                      .repeat_cooldown_reset()
                      .offset(),
              "Offset of repeat cooldown reset field does not match.");

} // namespace commandstation



#endif // _COMMANDSTATION_PROGRAMMINGTRACKSPACECONFIG_HXX_