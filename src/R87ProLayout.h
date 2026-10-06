/*---------------------------------------------------------*\
| R87ProLayout.h                                            |
|                                                           |
|   LED index <-> key position table for the Royal Kludge   |
|   R87 Pro (BY Tech / SinoWealth 258A:019F) TKL keyboard.  |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>

namespace r87pro
{
    /*-----------------------------------------------------*\
    | Value written into empty matrix cells                 |
    \*-----------------------------------------------------*/
    const unsigned int MATRIX_UNUSED = 0xFFFFFFFFu;

    /*-----------------------------------------------------*\
    | One physical key of the 87-key TKL layout             |
    \*-----------------------------------------------------*/
    struct KeyEntry
    {
        std::string  name;      /* OpenRGB standard key name ("Key: Escape") */
        unsigned int led;       /* index in the vendor RGB buffer            */
        unsigned int row;       /* 0 = function row, 5 = bottom row          */
        unsigned int col;       /* 0 = leftmost matrix column                */
    };

    /*-----------------------------------------------------*\
    | Complete layout description                           |
    \*-----------------------------------------------------*/
    struct Layout
    {
        std::vector<KeyEntry> keys;
        unsigned int          width     = 0;    /* matrix columns        */
        unsigned int          height    = 0;    /* matrix rows (6)       */
        unsigned int          led_count = 0;    /* highest LED index + 1 */

        /*-------------------------------------------------*\
        | Recompute width/height/led_count from the keys    |
        \*-------------------------------------------------*/
        void Finalize();

        /*-------------------------------------------------*\
        | Build the OpenRGB matrix map (height * width),    |
        | MATRIX_UNUSED in cells without a key              |
        \*-------------------------------------------------*/
        std::vector<unsigned int> BuildMatrixMap() const;

        /*-------------------------------------------------*\
        | Apply per-key LED index overrides from a JSON     |
        | file: { "keys": { "Key: Escape": 0, ... } }       |
        | Missing file is not an error (returns false with  |
        | an empty error string).                           |
        \*-------------------------------------------------*/
        bool ApplyJsonOverrides(const std::string& path, std::string& error);

        /*-------------------------------------------------*\
        | Find a key by name (nullptr when not present)     |
        \*-------------------------------------------------*/
        const KeyEntry* FindKey(const std::string& name) const;
    };

    /*-----------------------------------------------------*\
    | Default ANSI TKL layout (87 keys)                     |
    \*-----------------------------------------------------*/
    Layout GetDefaultTKL();

    /*-----------------------------------------------------*\
    | Human readable name for a SinoWealth model ID         |
    \*-----------------------------------------------------*/
    std::string ModelName(unsigned char model_id);
}
