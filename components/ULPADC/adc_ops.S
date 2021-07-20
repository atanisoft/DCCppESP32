/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2021 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#include "sdkconfig.h"

#include <soc/rtc_cntl_reg.h>
#include <soc/soc_ulp.h>

    /* Configure the number of ADC samples to average on each measurement.
       Must be a power of 2. */
    .set adc_sample_count_log, 2
    .set adc_sample_count, (1 << adc_sample_count_log)

    /* Start of variable declaration section, all zero initialized. */
    .bss

    /* Count of samples taken by the ULP */
    .global exec_count
exec_count:
    .long 0

#if !CONFIG_TEMPSENSOR_DISABLED
    /* last reading from the TEMPSENSOR ADC channel. */
    .global tempsensor_last_reading
tempsensor_last_reading:
    .long 0
#endif // !CONFIG_TEMPSENSOR_DISABLED

#if CONFIG_OPS_TRACK_ENABLED
    /* OPS short threshold, configured by main CPU.
       If the detected ADC value exceeds this the main CPU will be alerted to
       wake up.
     */
    .global ops_short_threshold
ops_short_threshold:
    .long 0

    /* OPS shutdown threshold, configured by main CPU.
       If the detected ADC value exceeds this the main CPU will be alerted to
       wake up.
     */
    .global ops_shutdown_threshold
ops_shutdown_threshold:
    .long 0

    /* OPS warning threshold, configured by main CPU.
       If the detected ADC value exceeds this the main CPU will be alerted to
       wake up.
     */
    .global ops_warning_threshold
ops_warning_threshold:
    .long 0

    /* last reading from the OPS ADC channel. */
    .global ops_last_reading
ops_last_reading:
    .long 0
#endif // CONFIG_OPS_TRACK_ENABLED

#if CONFIG_PROG_TRACK_ENABLED
    /* PROG ack threshold, configured by main CPU.
       If the detected ADC value exceeds this the main CPU will be alerted to
       wake up.
     */
    .global prog_ack_threshold
prog_ack_threshold:
    .long 0

    /* PROG short threshold, configured by main CPU.
       If the detected ADC value exceeds this the main CPU will be alerted to
       wake up.
     */
    .global prog_short_threshold
prog_short_threshold:
    .long 0

    /* last reading from the PROG ADC channel. */
    .global prog_last_reading
prog_last_reading:
    .long 0
#endif // CONFIG_PROG_TRACK_ENABLED

    /* Main entry point of the ULP ADC reading code. */
    .text
    .global entry
entry:

    /* Increment the number of executions. */
    move r3, exec_count
    ld r0, r3, 0
    add r0, r0, 1
    st r0, r3, 0

    /* Reset accumulator registers to zero */
    move r0, 0
    move r1, 0
    move r2, 0

    /* initialize counter so we can loop through N samples */
    stage_rst

measure:
#if !CONFIG_TEMPSENSOR_DISABLED
    /* measure and add value to accumulator R0 */
#if CONFIG_TEMPSENSOR_ADC_CHANNEL_0
    adc r3, 0, 1
#elif CONFIG_TEMPSENSOR_ADC_CHANNEL_3
    adc r3, 0, 4
#elif CONFIG_TEMPSENSOR_ADC_CHANNEL_4
    adc r3, 0, 5
#elif CONFIG_TEMPSENSOR_ADC_CHANNEL_5
    adc r3, 0, 6
#elif CONFIG_TEMPSENSOR_ADC_CHANNEL_6
    adc r3, 0, 7
#elif CONFIG_TEMPSENSOR_ADC_CHANNEL_7
    adc r3, 0, 8
#endif
    add r0, r0, r3
#endif // !CONFIG_TEMPSENSOR_DISABLED

#if CONFIG_OPS_TRACK_ENABLED
    /* measure and add value to accumulator R1 */
#if CONFIG_OPSTRACK_ADC_CHANNEL_0
    adc r3, 0, 1
#elif CONFIG_OPSTRACK_ADC_CHANNEL_3
    adc r3, 0, 4
#elif CONFIG_OPSTRACK_ADC_CHANNEL_4
    adc r3, 0, 5
#elif CONFIG_OPSTRACK_ADC_CHANNEL_5
    adc r3, 0, 6
#elif CONFIG_OPSTRACK_ADC_CHANNEL_6
    adc r3, 0, 7
#elif CONFIG_OPSTRACK_ADC_CHANNEL_7
    adc r3, 0, 8
#endif
    add r1, r1, r3
#endif // CONFIG_OPS_TRACK_ENABLED

#if CONFIG_PROG_TRACK_ENABLED
    /* measure and add value to accumulator R2 */
#if CONFIG_PROGTRACK_ADC_CHANNEL_0
    adc r3, 0, 1
#elif CONFIG_PROGTRACK_ADC_CHANNEL_3
    adc r3, 0, 4
#elif CONFIG_PROGTRACK_ADC_CHANNEL_4
    adc r3, 0, 5
#elif CONFIG_PROGTRACK_ADC_CHANNEL_5
    adc r3, 0, 6
#elif CONFIG_PROGTRACK_ADC_CHANNEL_6
    adc r3, 0, 7
#elif CONFIG_PROGTRACK_ADC_CHANNEL_7
    adc r3, 0, 8
#endif
    add r2, r2, r3
#endif // CONFIG_PROG_TRACK_ENABLED

    /* increment loop count and check exit condition */
    stage_inc 1
    jumps measure, adc_sample_count, lt
    stage_rst

#if !CONFIG_TEMPSENSOR_DISABLED
    /* divide accumulator value using bit shifting */
    rsh     r0, r0, adc_sample_count_log

    /* store the value for usage by the main CPU */
    move    r3, tempsensor_last_reading
    st      r0, r3, 0
#endif // !CONFIG_TEMPSENSOR_DISABLED

#if CONFIG_OPS_TRACK_ENABLED
    /* divide accumulator value using bit shifting */
    rsh     r1, r1, adc_sample_count_log

    /* store the value for usage by the main CPU */
    move    r3, ops_last_reading
    st      r1, r3, 0
#endif // CONFIG_OPS_TRACK_ENABLED

#if CONFIG_PROG_TRACK_ENABLED
    /* divide accumulator value using bit shifting */
    rsh     r2, r2, adc_sample_count_log

    /* store the value for usage by the main CPU */
    move    r3, prog_last_reading
    st      r2, r3, 0
#endif // CONFIG_PROG_TRACK_ENABLED

#if CONFIG_OPS_TRACK_ENABLED
    /* Wakeup SoC if ops_last_reading > ops_short_threshold */
    move    r3, ops_short_threshold
    ld      r3, r3, 0
    sub     r3, r3, r1
    jump    wake_up, ov
#endif // CONFIG_OPS_TRACK_ENABLED

#if CONFIG_PROG_TRACK_ENABLED
    /* Wakeup SoC if prog_last_reading > prog_ack_threshold.
    NOTE: prog_short_threshold is not checked as it should be higher than the
          prog_ack_threshold value.*/
    move    r3, prog_ack_threshold
    ld      r3, r3, 0
    sub     r3, r3, r2
    jump    wake_up, ov
#endif // CONFIG_PROG_TRACK_ENABLED

    .global exit
exit:
    halt

    .global wake_up
wake_up:
    /* Wake up the SoC, end program */
    wake
    halt