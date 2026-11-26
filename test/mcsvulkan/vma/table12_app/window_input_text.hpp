#pragma once
// ============================================================================
// table12_app/window_input_text.hpp — input text：map_key / encode_utf8
//
//   依赖：host.hpp。
// ============================================================================

#include "host.hpp"

namespace table::window
{
    inline std::optional<table::command::Key> map_key(ev::Key k) noexcept
    {
        switch (k)
        {
        case ev::Key::eUP:
            return table::command::Key::up;
        case ev::Key::eDOWN:
            return table::command::Key::down;
        case ev::Key::eLEFT:
            return table::command::Key::left;
        case ev::Key::eRIGHT:
            return table::command::Key::right;
        case ev::Key::eHOME:
            return table::command::Key::home;
        case ev::Key::eEND:
            return table::command::Key::end;
        case ev::Key::ePAGE_UP:
            return table::command::Key::pgup;
        case ev::Key::ePAGE_DOWN:
            return table::command::Key::pgdn;
        case ev::Key::eENTER:
            return table::command::Key::enter;
        case ev::Key::eESCAPE:
            return table::command::Key::esc;
        case ev::Key::eTAB:
            return table::command::Key::tab;
        case ev::Key::eBACKSPACE:
            return table::command::Key::backspace;
        case ev::Key::eDELETE:
            return table::command::Key::del;
        case ev::Key::eSPACE:
            return table::command::Key::space;
        case ev::Key::eF2:
            return table::command::Key::f2;
        case ev::Key::eA:
            return table::command::Key::a;
        case ev::Key::eC:
            return table::command::Key::c;
        case ev::Key::eV:
            return table::command::Key::v;
        case ev::Key::eX:
            return table::command::Key::x;
        case ev::Key::eZ:
            return table::command::Key::z;
        case ev::Key::eY:
            return table::command::Key::y;
        case ev::Key::eF:
            return table::command::Key::f;
        default:
            return std::nullopt;
        }
    }


    inline void encode_utf8(std::uint32_t cp, std::string &out)
    {
        if (cp < 0x80)
            out += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

} // namespace table::window
