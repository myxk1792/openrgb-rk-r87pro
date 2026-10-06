/*---------------------------------------------------------*\
| R87ProLayout.cpp                                          |
|                                                           |
|   LED index <-> key position table for the Royal Kludge   |
|   R87 Pro (BY Tech / SinoWealth 258A:019F) TKL keyboard.  |
|                                                           |
|   The SinoWealth vendor protocol addresses LEDs by a flat |
|   index; for this firmware the index follows the matrix   |
|   wiring:  index = column * 6 + row.                      |
|                                                           |
|   Every index in the table below was verified against the |
|   real keyboard (model ID 0x56) by lighting one LED at a  |
|   time: Escape=0, backtick=1, F1=6, 1=7, F2=12, 2=13,     |
|   Space=35, Menu=65, F12=72, Calculator=78, Enter=81,     |
|   RShift=82, RCtrl=83, PrtSc=84, Ins=85, Del=86, Left=89, |
|   Scroll Lock=90, Home=91, End=92, Up=94, Down=95,        |
|   Pause=96, PgUp=97, PgDn=98, Right=101, LCtrl=5,         |
|   RAlt=53, Fn=59.                                          |
|                                                           |
|   If a particular board revision numbers its LEDs        |
|   differently, put a "rk-r87pro-layout.json" override in  |
|   the OpenRGB configuration directory (see README).       |
|                                                           |
|   This file is part of the RK R87 Pro OpenRGB plugin      |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include "R87ProLayout.h"

namespace r87pro
{

/*---------------------------------------------------------*\
| Key names (same strings OpenRGB uses so the keyboard view |
| recognizes them)                                          |
\*---------------------------------------------------------*/
#define K_ESC       "Key: Escape"
#define K_F(n)      "Key: F" #n
#define K_CALC      "Key: Calculator"
#define K_PRT       "Key: Print Screen"
#define K_SCR       "Key: Scroll Lock"
#define K_PAU       "Key: Pause/Break"
#define K_TICK      "Key: `"
#define K_BSPC      "Key: Backspace"
#define K_INS       "Key: Insert"
#define K_HOME      "Key: Home"
#define K_PGUP      "Key: Page Up"
#define K_TAB       "Key: Tab"
#define K_LBRK      "Key: ["
#define K_RBRK      "Key: ]"
#define K_BSLS      "Key: \\"
#define K_DEL       "Key: Delete"
#define K_END       "Key: End"
#define K_PGDN      "Key: Page Down"
#define K_CAPS      "Key: Caps Lock"
#define K_ENTER     "Key: Enter"
#define K_LSFT      "Key: Left Shift"
#define K_RSFT      "Key: Right Shift"
#define K_UP        "Key: Up Arrow"
#define K_LCTL      "Key: Left Control"
#define K_LWIN      "Key: Left Windows"
#define K_LALT      "Key: Left Alt"
#define K_SPACE     "Key: Space"
#define K_RALT      "Key: Right Alt"
#define K_RCTL      "Key: Right Control"
#define K_LEFT      "Key: Left Arrow"
#define K_DOWN      "Key: Down Arrow"
#define K_RIGHT     "Key: Right Arrow"

Layout GetDefaultTKL()
{
    Layout layout;

    const struct { const char* name; unsigned int led; } table[] =
    {
        /* Row 0 - function row (columns 1..13) plus the top-right
           cluster (columns 14..16).  Note F1 sits at column 1 (LED 6),
           not column 2 - measured on hardware.                      */
        { K_ESC,   0 }, { K_F(1),  6 }, { K_F(2), 12 }, { K_F(3), 18 },
        { K_F(4), 24 }, { K_F(5), 30 }, { K_F(6), 36 }, { K_F(7), 42 },
        { K_F(8), 48 }, { K_F(9), 54 }, { "Key: F10", 60 }, { "Key: F11", 66 },
        { "Key: F12", 72 }, { K_CALC, 78 }, { K_PRT, 84 }, { K_SCR, 90 }, { K_PAU, 96 },

        /* Row 1 */
        { K_TICK,  1 }, { "Key: 1",  7 }, { "Key: 2", 13 }, { "Key: 3", 19 },
        { "Key: 4", 25 }, { "Key: 5", 31 }, { "Key: 6", 37 }, { "Key: 7", 43 },
        { "Key: 8", 49 }, { "Key: 9", 55 }, { "Key: 0", 61 }, { "Key: -", 67 },
        { "Key: =", 73 }, { K_BSPC, 79 }, { K_INS, 85 }, { K_HOME, 91 }, { K_PGUP, 97 },

        /* Row 2 */
        { K_TAB,   2 }, { "Key: Q",  8 }, { "Key: W", 14 }, { "Key: E", 20 },
        { "Key: R", 26 }, { "Key: T", 32 }, { "Key: Y", 38 }, { "Key: U", 44 },
        { "Key: I", 50 }, { "Key: O", 56 }, { "Key: P", 62 }, { K_LBRK, 68 },
        { K_RBRK, 74 }, { K_BSLS, 80 }, { K_DEL, 86 }, { K_END, 92 }, { K_PGDN, 98 },

        /* Row 3 */
        { K_CAPS,  3 }, { "Key: A",  9 }, { "Key: S", 15 }, { "Key: D", 21 },
        { "Key: F", 27 }, { "Key: G", 33 }, { "Key: H", 39 }, { "Key: J", 45 },
        { "Key: K", 51 }, { "Key: L", 57 }, { "Key: ;", 63 }, { "Key: '", 69 },
        { K_ENTER, 81 },

        /* Row 4 */
        { K_LSFT,  4 }, { "Key: Z", 10 }, { "Key: X", 16 }, { "Key: C", 22 },
        { "Key: V", 28 }, { "Key: B", 34 }, { "Key: N", 40 }, { "Key: M", 46 },
        { "Key: ,", 52 }, { "Key: .", 58 }, { "Key: /", 64 }, { K_RSFT, 82 },
        { K_UP,   94 },

        /* Row 5 */
        { K_LCTL,  5 }, { K_LWIN, 11 }, { K_LALT, 17 }, { K_SPACE, 35 },
        { K_RALT, 53 }, { "Key: Fn", 59 }, { "Key: Menu", 65 }, { K_RCTL, 83 },
        { K_LEFT, 89 }, { K_DOWN, 95 }, { K_RIGHT, 101 },
    };

    for(const auto& entry : table)
    {
        KeyEntry key;
        key.name = entry.name;
        key.led  = entry.led;
        key.row  = entry.led % 6;
        key.col  = entry.led / 6;
        layout.keys.push_back(key);
    }

    layout.Finalize();

    return(layout);
}

void Layout::Finalize()
{
    width     = 0;
    height    = 0;
    led_count = 0;

    for(const KeyEntry& key : keys)
    {
        width     = (key.col + 1 > width)  ? key.col + 1 : width;
        height    = (key.row + 1 > height) ? key.row + 1 : height;
        led_count = (key.led + 1 > led_count) ? key.led + 1 : led_count;
    }

    if(height == 0)
    {
        height = 6;
    }
}

std::vector<unsigned int> Layout::BuildMatrixMap() const
{
    std::vector<unsigned int> map((size_t)height * width, MATRIX_UNUSED);

    for(const KeyEntry& key : keys)
    {
        map[(size_t)key.row * width + key.col] = key.led;
    }

    return(map);
}

const KeyEntry* Layout::FindKey(const std::string& name) const
{
    for(const KeyEntry& key : keys)
    {
        if(key.name == name)
        {
            return(&key);
        }
    }

    return(nullptr);
}

bool Layout::ApplyJsonOverrides(const std::string& path, std::string& error)
{
    error.clear();

    std::ifstream file(path);

    if(!file.is_open())
    {
        return(false);
    }

    nlohmann::json root;

    try
    {
        file >> root;
    }
    catch(const std::exception& e)
    {
        error = std::string("cannot parse ") + path + ": " + e.what();
        return(false);
    }

    if(!root.contains("keys") || !root["keys"].is_object())
    {
        error = path + ": missing \"keys\" object";
        return(false);
    }

    for(auto it = root["keys"].begin(); it != root["keys"].end(); ++it)
    {
        KeyEntry* key = nullptr;

        for(KeyEntry& candidate : keys)
        {
            if(candidate.name == it.key())
            {
                key = &candidate;
                break;
            }
        }

        if(key == nullptr)
        {
            error += "unknown key '" + it.key() + "' ignored; ";
            continue;
        }

        if(!it.value().is_number())
        {
            error += "value for '" + it.key() + "' is not a number, ignored; ";
            continue;
        }

        key->led = it.value().get<unsigned int>();
    }

    Finalize();

    return(true);
}

std::string ModelName(unsigned char model_id)
{
    static const std::map<unsigned char, std::string> models =
    {
        { 0x56, "Royal Kludge R87 Pro" },
        { 0x0B, "AULA F87 Pro (same SinoWealth TKL family)" },
        { 0xA3, "LEOBOG Hi75C Pro" },
        { 0xA4, "AULA F99" },
        { 0xCD, "AULA F75" },
        { 0x20, "Redragon K686 Eisa Pro" },
        { 0x05, "Redragon K686 Eisa Pro" },
    };

    auto it = models.find(model_id);

    if(it != models.end())
    {
        return(it->second);
    }

    return("unknown model ID");
}

}   /* namespace r87pro */
