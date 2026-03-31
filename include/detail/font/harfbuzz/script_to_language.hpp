#pragma once

#include "../__harfbuzz_import.hpp"

namespace mcs::vulkan::font::harfbuzz
{
    constexpr static hb_language_t script_to_language(hb_script_t script) noexcept
    {
        switch (script)
        {
        // Unicode 1.1
        case HB_SCRIPT_ARABIC:
            return hb_language_from_string("ar", -1);
            break;
        case HB_SCRIPT_ARMENIAN:
            return hb_language_from_string("hy", -1);
            break;
        case HB_SCRIPT_BENGALI:
            return hb_language_from_string("bn", -1);
            break;
        case HB_SCRIPT_CANADIAN_ABORIGINAL:
            return hb_language_from_string("iu", -1);
            break;
        case HB_SCRIPT_CHEROKEE:
            return hb_language_from_string("chr", -1);
            break;
        case HB_SCRIPT_COPTIC:
            return hb_language_from_string("cop", -1);
            break;
        case HB_SCRIPT_CYRILLIC:
            return hb_language_from_string("ru", -1);
            break;
        case HB_SCRIPT_DEVANAGARI:
            return hb_language_from_string("hi", -1);
            break;
        case HB_SCRIPT_GEORGIAN:
            return hb_language_from_string("ka", -1);
            break;
        case HB_SCRIPT_GREEK:
            return hb_language_from_string("el", -1);
            break;
        case HB_SCRIPT_GUJARATI:
            return hb_language_from_string("gu", -1);
            break;
        case HB_SCRIPT_GURMUKHI:
            return hb_language_from_string("pa", -1);
            break;
        case HB_SCRIPT_HANGUL:
            return hb_language_from_string("ko", -1);
            break;
        case HB_SCRIPT_HAN:
            return hb_language_from_string("zh-hans", -1);
            break;
        case HB_SCRIPT_HEBREW:
            return hb_language_from_string("he", -1);
            break;
        case HB_SCRIPT_HIRAGANA:
            return hb_language_from_string("ja", -1);
            break;
        case HB_SCRIPT_KANNADA:
            return hb_language_from_string("kn", -1);
            break;
        case HB_SCRIPT_KATAKANA:
            return hb_language_from_string("ja", -1);
            break;
        case HB_SCRIPT_LAO:
            return hb_language_from_string("lo", -1);
            break;
        case HB_SCRIPT_LATIN:
            return hb_language_from_string("en", -1);
            break;
        case HB_SCRIPT_MALAYALAM:
            return hb_language_from_string("ml", -1);
            break;
        case HB_SCRIPT_MONGOLIAN:
            return hb_language_from_string("mn", -1);
            break;
        case HB_SCRIPT_ORIYA:
            return hb_language_from_string("or", -1);
            break;
        case HB_SCRIPT_SYRIAC:
            return hb_language_from_string("syr", -1);
            break;
        case HB_SCRIPT_TAMIL:
            return hb_language_from_string("ta", -1);
            break;
        case HB_SCRIPT_TELUGU:
            return hb_language_from_string("te", -1);
            break;
        case HB_SCRIPT_THAI:
            return hb_language_from_string("th", -1);
            break;

        // Unicode 2.0
        case HB_SCRIPT_TIBETAN:
            return hb_language_from_string("bo", -1);
            break;

        // Unicode 3.0
        case HB_SCRIPT_ETHIOPIC:
            return hb_language_from_string("am", -1);
            break;
        case HB_SCRIPT_KHMER:
            return hb_language_from_string("km", -1);
            break;
        case HB_SCRIPT_MYANMAR:
            return hb_language_from_string("my", -1);
            break;
        case HB_SCRIPT_SINHALA:
            return hb_language_from_string("si", -1);
            break;
        case HB_SCRIPT_THAANA:
            return hb_language_from_string("dv", -1);
            break;

        // Unicode 3.2
        case HB_SCRIPT_BUHID:
            return hb_language_from_string("bku", -1);
            break;
        case HB_SCRIPT_HANUNOO:
            return hb_language_from_string("hnn", -1);
            break;
        case HB_SCRIPT_TAGALOG:
            return hb_language_from_string("tl", -1);
            break;
        case HB_SCRIPT_TAGBANWA:
            return hb_language_from_string("tbw", -1);
            break;

        // Unicode 4.0
        case HB_SCRIPT_UGARITIC:
            return hb_language_from_string("uga", -1);
            break;

        // Unicode 4.1
        case HB_SCRIPT_BUGINESE:
            return hb_language_from_string("bug", -1);
            break;
        case HB_SCRIPT_OLD_PERSIAN:
            return hb_language_from_string("peo", -1);
            break;
        case HB_SCRIPT_SYLOTI_NAGRI:
            return hb_language_from_string("syl", -1);
            break;

        // Unicode 5.0
        case HB_SCRIPT_NKO:
            return hb_language_from_string("nko", -1);
            break;

        // no representative language exists
        default:
            return HB_LANGUAGE_INVALID;
            break;
        }
    }
}; // namespace mcs::vulkan::font::harfbuzz