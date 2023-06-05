/****************************************************************************
 * frameworks/media/media_dtmf.c
 *
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "media_dtmf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/** To ensure the quality and stability of dtmf tones, a sample_rate of
 *  8000Hz is usually chosen. sampling time is 150 milliseconds and channel
 *  is mono.So, framesize = (sample_rate * channel * sampling time/1000 ).
 */

#define FRAME_SIZE 1200

/** 31000 ensures that the initial value is within a reasonable range and
 * avoids distortion and noise interference.
 */

#define INITIAL_STATE 31000

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_dtmf_calculate_product
 *
 * Description:
 *   Calculate the product of two integers It first converts the Integer
 *   Integer_one and Integer_two Multiply the lower 16 bits of two.then shift
 *   the result 15 bits to the right to obtain a 16 bit intermediate
 *   result temp0.Next, it converts the Integer_one and Integer_two Multiply
 *   the upper 16 bits of two to obtain a 32-bit intermediate result temp1.
 *   Finally, the function multiplies the sum of temp0 and temp1 by 2.
 *
 * Input Parameters:
 *   integer_one - Product integer one.
 *   integer_two - Product integer one.
 *
 * Returned Value:
 *   A multiplicative product.
 *
 ****************************************************************************/

static inline int media_dtmf_calculate_product(short integer_one, int integer_two)
{
    unsigned short temp0;
    unsigned int temp1;

    temp0 = (((unsigned short)integer_two * integer_one) + 0x4000) >> 15;
    temp1 = (short)(integer_two >> 16) * integer_one;

    return (temp1 << 1) + temp0;
}

/****************************************************************************
 * Name: media_dtmf_frequency_oscillator
 *
 * Description:
 *   This code implements a frequency oscillator for DTMF signals.
 *   which is used to generate DTMF signals composed of low-frequency and
 *   high-frequency signals.The value of each sample is obtained by
 *   the sum of the products of low-frequency and high-frequency signals.
 *   four state value state variables can ensure the continuity and stability
 *   of DTMF signals, thereby providing high-quality audio signal output.
 *
 * Input Parameters:
 *   low_freq_coeff - Represents the coefficient of a low-frequency signal.
 *   high_freq_coeff - Represents the coefficient of a high-frequency signal.
 *   buffer - Used to store the generated DTMF signal.
 *   count - Indicates the number of samples to generate DTMF signals.
 *   prev_low_freq_state_one - Low-frequency signal state.
 *   prev_high_freq_state_one - High-frequency signal state.
 *   prev_low_freq_state_two - Low-frequency signal statte.
 *   prev_high_freq_state_two - High-frequency signal state.
 *
 ****************************************************************************/

static void media_dtmf_frequency_oscillator(short low_freq_coeff, short high_freq_coeff,
    short int* buffer, unsigned int count,
    int* prev_low_freq_state_one, int* prev_high_freq_state_one,
    int* prev_low_freq_state_two, int* prev_high_freq_state_two)
{
    unsigned int temp1_0, temp1_1, temp2_0, temp2_1, temp0, temp1, subject;
    unsigned short ii;

    temp1_0 = *prev_low_freq_state_one;
    temp1_1 = *prev_high_freq_state_one;
    temp2_0 = *prev_low_freq_state_two;
    temp2_1 = *prev_high_freq_state_two;

    subject = low_freq_coeff * high_freq_coeff;
    for (ii = 0; ii < count; ++ii) {
        temp0 = media_dtmf_calculate_product(low_freq_coeff, temp1_0 << 1) - temp2_0;
        temp1 = media_dtmf_calculate_product(high_freq_coeff, temp1_1 << 1) - temp2_1;
        temp2_0 = temp1_0;
        temp2_1 = temp1_1;
        temp1_0 = temp0;
        temp1_1 = temp1;
        temp0 += temp1;

        if (subject)
            temp0 >>= 1;
        buffer[ii] = (short)temp0;
    }

    *prev_low_freq_state_one = temp1_0;
    *prev_high_freq_state_one = temp1_1;
    *prev_low_freq_state_two = temp2_0;
    *prev_high_freq_state_two = temp2_1;
}

/****************************************************************************
 * Name: media_dtmf_generating
 *
 * Description:
 *   This code is a function used to generate dual tone multi frequency
 *   signals. DTMF signals are composed of two frequency signals and are
 *   typically used in telephone dialing systems. This function selects
 *   the corresponding low-frequency and high-frequency coefficients.
 *   on the input dial button number, and uses these coefficients as inputs
 *   to generate a DTMF signal in the given buffer.
 *
 * Input Parameters:
 *   numbers - Dialbuttons numbers.
 *   buffer - Used to store the generated DTMF signal.
 *
 ****************************************************************************/

static void media_dtmf_generating(char numbers, short int* buffer)
{
    const short freq_coeff[8] = {
        /* Low frequencies (row) */
        27980, /* 697Hz */
        26956, /* 770Hz */
        25701, /* 852Hz */
        24218, /* 941Hz */
        /* High frequencies (column) */
        19073, /* 1209Hz */
        16325, /* 1336Hz */
        13085, /* 1477Hz */
        9315 /* 1633Hz */
    };

    short low_freq_coeff, high_freq_coeff;

    /** prev_low_freq_state_one and prev_low_freq_state_two Save the
     *  previous state of the low-frequency signal.prev_high_freq_state_one
     *  and prev_high_freq_state_two Save the previous state of the
     *  high-frequency signal. These variables are used to store the previous
     *  state of low-frequency and high-frequency signals.Each time a function
     *  is called, these variables will be updated to the current state and
     *  used for the next call. This helps to maintain the continuity and
     *  stability of the signal to generate normal dual tone multi frequency
     *  signals.
     */

    int prev_low_freq_state_one, prev_high_freq_state_one, prev_low_freq_state_two, prev_high_freq_state_two;

    switch (numbers) {
    case '0':
        low_freq_coeff = freq_coeff[3];
        high_freq_coeff = freq_coeff[5];
        prev_low_freq_state_one = freq_coeff[3];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[5];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '1':
        low_freq_coeff = freq_coeff[0];
        high_freq_coeff = freq_coeff[4];
        prev_low_freq_state_one = freq_coeff[0];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[4];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '2':
        low_freq_coeff = freq_coeff[0];
        high_freq_coeff = freq_coeff[5];
        prev_low_freq_state_one = freq_coeff[0];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[5];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '3':
        low_freq_coeff = freq_coeff[0];
        high_freq_coeff = freq_coeff[6];
        prev_low_freq_state_one = freq_coeff[0];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[6];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '4':
        low_freq_coeff = freq_coeff[1];
        high_freq_coeff = freq_coeff[4];
        prev_low_freq_state_one = freq_coeff[1];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[4];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '5':
        low_freq_coeff = freq_coeff[1];
        high_freq_coeff = freq_coeff[5];
        prev_low_freq_state_one = freq_coeff[1];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[5];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '6':
        low_freq_coeff = freq_coeff[1];
        high_freq_coeff = freq_coeff[6];
        prev_low_freq_state_one = freq_coeff[1];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[6];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '7':
        low_freq_coeff = freq_coeff[2];
        high_freq_coeff = freq_coeff[4];
        prev_low_freq_state_one = freq_coeff[2];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[4];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '8':
        low_freq_coeff = freq_coeff[2];
        high_freq_coeff = freq_coeff[5];
        prev_low_freq_state_one = freq_coeff[2];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[5];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '9':
        low_freq_coeff = freq_coeff[2];
        high_freq_coeff = freq_coeff[6];
        prev_low_freq_state_one = freq_coeff[2];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[6];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '*':
        low_freq_coeff = freq_coeff[3];
        high_freq_coeff = freq_coeff[4];
        prev_low_freq_state_one = freq_coeff[3];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[4];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case '#':
        low_freq_coeff = freq_coeff[3];
        high_freq_coeff = freq_coeff[6];
        prev_low_freq_state_one = freq_coeff[3];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[6];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case 'A':
        low_freq_coeff = freq_coeff[0];
        high_freq_coeff = freq_coeff[7];
        prev_low_freq_state_one = freq_coeff[0];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[7];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case 'B':
        low_freq_coeff = freq_coeff[1];
        high_freq_coeff = freq_coeff[7];
        prev_low_freq_state_one = freq_coeff[1];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[7];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case 'C':
        low_freq_coeff = freq_coeff[2];
        high_freq_coeff = freq_coeff[7];
        prev_low_freq_state_one = freq_coeff[2];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[7];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    case 'D':
        low_freq_coeff = freq_coeff[3];
        high_freq_coeff = freq_coeff[7];
        prev_low_freq_state_one = freq_coeff[3];
        prev_low_freq_state_two = INITIAL_STATE;
        prev_high_freq_state_one = freq_coeff[7];
        prev_high_freq_state_two = INITIAL_STATE;
        break;
    default:
        low_freq_coeff = high_freq_coeff = 0;
        prev_low_freq_state_one = 0;
        prev_low_freq_state_two = 0;
        prev_high_freq_state_one = 0;
        prev_high_freq_state_two = 0;
    }

    media_dtmf_frequency_oscillator(low_freq_coeff, high_freq_coeff,
        buffer, FRAME_SIZE,
        &prev_low_freq_state_one, &prev_high_freq_state_one,
        &prev_low_freq_state_two, &prev_high_freq_state_two);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_dtmf_generate
 *
 * Description:
 *   The purpose of this function is to generate one or continuous multiple
 *   DTMF signal and save the result in the input buffer.format should fixed
 *   as MEDIA_TONE_DTMF_FORMAT when to play dtmf tone.
 *
 * Input Parameters:
 *   numbers - Dialbuttons numbers.
 *   buffer - Used to store the generated DTMF signal.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_dtmf_generate(const char* numbers, short int* buffer)
{
    int i;
    int count;

    if (!numbers || !buffer || numbers[0] == '\0')
        return -EINVAL;

    count = strlen(numbers);

    for (i = 0; i < count; i++) {
        media_dtmf_generating(numbers[i], buffer + i * FRAME_SIZE);
    }

    return 0;
}

/****************************************************************************
 * Name: media_dtmf_get_buffer_size
 *
 * Description:
 *   The purpose of this function is to get one DTMF tone buffer size.
 *   The application need to multiply the count when generate multiple
 *   DTMF tones.
 *
 * Returned Value:
 *   Buffer size of one DTMF tone.
 *
 ****************************************************************************/

int media_dtmf_get_buffer_size(void)
{
    return FRAME_SIZE * sizeof(short int);
}