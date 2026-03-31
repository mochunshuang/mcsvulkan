#pragma once
#include "../__harfbuzz_import.hpp"
#include "../__bidi_import.hpp"

namespace mcs::vulkan::font::bidi
{
    /**
     * 将 SBScript 枚举值转换为 harfbuzz 的 hb_script_t 值。
     * 如果输入无效或未定义，返回 HB_SCRIPT_INVALID。
     */
    static constexpr hb_script_t sb_script_to_hb_script(SBScript script) noexcept
    {
        switch (script)
        {
        case SBScriptNil:
            return HB_SCRIPT_INVALID; // 无脚本
        case SBScriptZINH:
            return HB_SCRIPT_INHERITED;
        case SBScriptZYYY:
            return HB_SCRIPT_COMMON;
        case SBScriptZZZZ:
            return HB_SCRIPT_UNKNOWN;

        /* Unicode 1.1 */
        case SBScriptARAB:
            return HB_SCRIPT_ARABIC;
        case SBScriptARMN:
            return HB_SCRIPT_ARMENIAN;
        case SBScriptBENG:
            return HB_SCRIPT_BENGALI;
        case SBScriptBOPO:
            return HB_SCRIPT_BOPOMOFO;
        case SBScriptCYRL:
            return HB_SCRIPT_CYRILLIC;
        case SBScriptDEVA:
            return HB_SCRIPT_DEVANAGARI;
        case SBScriptGEOR:
            return HB_SCRIPT_GEORGIAN;
        case SBScriptGREK:
            return HB_SCRIPT_GREEK;
        case SBScriptGUJR:
            return HB_SCRIPT_GUJARATI;
        case SBScriptGURU:
            return HB_SCRIPT_GURMUKHI;
        case SBScriptHANG:
            return HB_SCRIPT_HANGUL;
        case SBScriptHANI:
            return HB_SCRIPT_HAN;
        case SBScriptHEBR:
            return HB_SCRIPT_HEBREW;
        case SBScriptHIRA:
            return HB_SCRIPT_HIRAGANA;
        case SBScriptKANA:
            return HB_SCRIPT_KATAKANA;
        case SBScriptKNDA:
            return HB_SCRIPT_KANNADA;
        case SBScriptLAOO:
            return HB_SCRIPT_LAO;
        case SBScriptLATN:
            return HB_SCRIPT_LATIN;
        case SBScriptMLYM:
            return HB_SCRIPT_MALAYALAM;
        case SBScriptORYA:
            return HB_SCRIPT_ORIYA;
        case SBScriptTAML:
            return HB_SCRIPT_TAMIL;
        case SBScriptTELU:
            return HB_SCRIPT_TELUGU;
        case SBScriptTHAI:
            return HB_SCRIPT_THAI;

        /* Unicode 2.0 */
        case SBScriptTIBT:
            return HB_SCRIPT_TIBETAN;

        /* Unicode 3.0 */
        case SBScriptBRAI:
            return HB_SCRIPT_BRAILLE;
        case SBScriptCANS:
            return HB_SCRIPT_CANADIAN_SYLLABICS;
        case SBScriptCHER:
            return HB_SCRIPT_CHEROKEE;
        case SBScriptETHI:
            return HB_SCRIPT_ETHIOPIC;
        case SBScriptKHMR:
            return HB_SCRIPT_KHMER;
        case SBScriptMONG:
            return HB_SCRIPT_MONGOLIAN;
        case SBScriptMYMR:
            return HB_SCRIPT_MYANMAR;
        case SBScriptOGAM:
            return HB_SCRIPT_OGHAM;
        case SBScriptRUNR:
            return HB_SCRIPT_RUNIC;
        case SBScriptSINH:
            return HB_SCRIPT_SINHALA;
        case SBScriptSYRC:
            return HB_SCRIPT_SYRIAC;
        case SBScriptTHAA:
            return HB_SCRIPT_THAANA;
        case SBScriptYIII:
            return HB_SCRIPT_YI;

        /* Unicode 3.1 */
        case SBScriptDSRT:
            return HB_SCRIPT_DESERET;
        case SBScriptGOTH:
            return HB_SCRIPT_GOTHIC;
        case SBScriptITAL:
            return HB_SCRIPT_OLD_ITALIC;

        /* Unicode 3.2 */
        case SBScriptBUHD:
            return HB_SCRIPT_BUHID;
        case SBScriptHANO:
            return HB_SCRIPT_HANUNOO;
        case SBScriptTAGB:
            return HB_SCRIPT_TAGBANWA;
        case SBScriptTGLG:
            return HB_SCRIPT_TAGALOG;

        /* Unicode 4.0 */
        case SBScriptCPRT:
            return HB_SCRIPT_CYPRIOT;
        case SBScriptLIMB:
            return HB_SCRIPT_LIMBU;
        case SBScriptLINB:
            return HB_SCRIPT_LINEAR_B;
        case SBScriptOSMA:
            return HB_SCRIPT_OSMANYA;
        case SBScriptSHAW:
            return HB_SCRIPT_SHAVIAN;
        case SBScriptTALE:
            return HB_SCRIPT_TAI_LE;
        case SBScriptUGAR:
            return HB_SCRIPT_UGARITIC;

        /* Unicode 4.1 */
        case SBScriptBUGI:
            return HB_SCRIPT_BUGINESE;
        case SBScriptCOPT:
            return HB_SCRIPT_COPTIC;
        case SBScriptGLAG:
            return HB_SCRIPT_GLAGOLITIC;
        case SBScriptKHAR:
            return HB_SCRIPT_KHAROSHTHI;
        case SBScriptSYLO:
            return HB_SCRIPT_SYLOTI_NAGRI;
        case SBScriptTALU:
            return HB_SCRIPT_NEW_TAI_LUE;
        case SBScriptTFNG:
            return HB_SCRIPT_TIFINAGH;
        case SBScriptXPEO:
            return HB_SCRIPT_OLD_PERSIAN;

        /* Unicode 5.0 */
        case SBScriptBALI:
            return HB_SCRIPT_BALINESE;
        case SBScriptNKOO:
            return HB_SCRIPT_NKO;
        case SBScriptPHAG:
            return HB_SCRIPT_PHAGS_PA;
        case SBScriptPHNX:
            return HB_SCRIPT_PHOENICIAN;
        case SBScriptXSUX:
            return HB_SCRIPT_CUNEIFORM;

        /* Unicode 5.1 */
        case SBScriptCARI:
            return HB_SCRIPT_CARIAN;
        case SBScriptCHAM:
            return HB_SCRIPT_CHAM;
        case SBScriptKALI:
            return HB_SCRIPT_KAYAH_LI;
        case SBScriptLEPC:
            return HB_SCRIPT_LEPCHA;
        case SBScriptLYCI:
            return HB_SCRIPT_LYCIAN;
        case SBScriptLYDI:
            return HB_SCRIPT_LYDIAN;
        case SBScriptOLCK:
            return HB_SCRIPT_OL_CHIKI;
        case SBScriptRJNG:
            return HB_SCRIPT_REJANG;
        case SBScriptSAUR:
            return HB_SCRIPT_SAURASHTRA;
        case SBScriptSUND:
            return HB_SCRIPT_SUNDANESE;
        case SBScriptVAII:
            return HB_SCRIPT_VAI;

        /* Unicode 5.2 */
        case SBScriptARMI:
            return HB_SCRIPT_IMPERIAL_ARAMAIC;
        case SBScriptAVST:
            return HB_SCRIPT_AVESTAN;
        case SBScriptBAMU:
            return HB_SCRIPT_BAMUM;
        case SBScriptEGYP:
            return HB_SCRIPT_EGYPTIAN_HIEROGLYPHS;
        case SBScriptJAVA:
            return HB_SCRIPT_JAVANESE;
        case SBScriptKTHI:
            return HB_SCRIPT_KAITHI;
        case SBScriptLANA:
            return HB_SCRIPT_TAI_THAM;
        case SBScriptLISU:
            return HB_SCRIPT_LISU;
        case SBScriptMTEI:
            return HB_SCRIPT_MEETEI_MAYEK;
        case SBScriptORKH:
            return HB_SCRIPT_OLD_TURKIC;
        case SBScriptPHLI:
            return HB_SCRIPT_INSCRIPTIONAL_PAHLAVI;
        case SBScriptPRTI:
            return HB_SCRIPT_INSCRIPTIONAL_PARTHIAN;
        case SBScriptSAMR:
            return HB_SCRIPT_SAMARITAN;
        case SBScriptSARB:
            return HB_SCRIPT_OLD_SOUTH_ARABIAN;
        case SBScriptTAVT:
            return HB_SCRIPT_TAI_VIET;

        /* Unicode 6.0 */
        case SBScriptBATK:
            return HB_SCRIPT_BATAK;
        case SBScriptBRAH:
            return HB_SCRIPT_BRAHMI;
        case SBScriptMAND:
            return HB_SCRIPT_MANDAIC;

        /* Unicode 6.1 */
        case SBScriptCAKM:
            return HB_SCRIPT_CHAKMA;
        case SBScriptMERC:
            return HB_SCRIPT_MEROITIC_CURSIVE;
        case SBScriptMERO:
            return HB_SCRIPT_MEROITIC_HIEROGLYPHS;
        case SBScriptPLRD:
            return HB_SCRIPT_MIAO;
        case SBScriptSHRD:
            return HB_SCRIPT_SHARADA;
        case SBScriptSORA:
            return HB_SCRIPT_SORA_SOMPENG;
        case SBScriptTAKR:
            return HB_SCRIPT_TAKRI;

        /* Unicode 7.0 */
        case SBScriptAGHB:
            return HB_SCRIPT_CAUCASIAN_ALBANIAN;
        case SBScriptBASS:
            return HB_SCRIPT_BASSA_VAH;
        case SBScriptDUPL:
            return HB_SCRIPT_DUPLOYAN;
        case SBScriptELBA:
            return HB_SCRIPT_ELBASAN;
        case SBScriptGRAN:
            return HB_SCRIPT_GRANTHA;
        case SBScriptHMNG:
            return HB_SCRIPT_PAHAWH_HMONG;
        case SBScriptKHOJ:
            return HB_SCRIPT_KHOJKI;
        case SBScriptLINA:
            return HB_SCRIPT_LINEAR_A;
        case SBScriptMAHJ:
            return HB_SCRIPT_MAHAJANI;
        case SBScriptMANI:
            return HB_SCRIPT_MANICHAEAN;
        case SBScriptMEND:
            return HB_SCRIPT_MENDE_KIKAKUI;
        case SBScriptMODI:
            return HB_SCRIPT_MODI;
        case SBScriptMROO:
            return HB_SCRIPT_MRO;
        case SBScriptNARB:
            return HB_SCRIPT_OLD_NORTH_ARABIAN;
        case SBScriptNBAT:
            return HB_SCRIPT_NABATAEAN;
        case SBScriptPALM:
            return HB_SCRIPT_PALMYRENE;
        case SBScriptPAUC:
            return HB_SCRIPT_PAU_CIN_HAU;
        case SBScriptPERM:
            return HB_SCRIPT_OLD_PERMIC;
        case SBScriptPHLP:
            return HB_SCRIPT_PSALTER_PAHLAVI;
        case SBScriptSIDD:
            return HB_SCRIPT_SIDDHAM;
        case SBScriptSIND:
            return HB_SCRIPT_KHUDAWADI;
        case SBScriptTIRH:
            return HB_SCRIPT_TIRHUTA;
        case SBScriptWARA:
            return HB_SCRIPT_WARANG_CITI;

        /* Unicode 8.0 */
        case SBScriptAHOM:
            return HB_SCRIPT_AHOM;
        case SBScriptHATR:
            return HB_SCRIPT_HATRAN;
        case SBScriptHLUW:
            return HB_SCRIPT_ANATOLIAN_HIEROGLYPHS;
        case SBScriptHUNG:
            return HB_SCRIPT_OLD_HUNGARIAN;
        case SBScriptMULT:
            return HB_SCRIPT_MULTANI;
        case SBScriptSGNW:
            return HB_SCRIPT_SIGNWRITING;

        /* Unicode 9.0 */
        case SBScriptADLM:
            return HB_SCRIPT_ADLAM;
        case SBScriptBHKS:
            return HB_SCRIPT_BHAIKSUKI;
        case SBScriptMARC:
            return HB_SCRIPT_MARCHEN;
        case SBScriptNEWA:
            return HB_SCRIPT_NEWA;
        case SBScriptOSGE:
            return HB_SCRIPT_OSAGE;
        case SBScriptTANG:
            return HB_SCRIPT_TANGUT;

        /* Unicode 10.0 */
        case SBScriptGONM:
            return HB_SCRIPT_MASARAM_GONDI;
        case SBScriptNSHU:
            return HB_SCRIPT_NUSHU;
        case SBScriptSOYO:
            return HB_SCRIPT_SOYOMBO;
        case SBScriptZANB:
            return HB_SCRIPT_ZANABAZAR_SQUARE;

        /* Unicode 11.0 */
        case SBScriptDOGR:
            return HB_SCRIPT_DOGRA;
        case SBScriptGONG:
            return HB_SCRIPT_GUNJALA_GONDI;
        case SBScriptMAKA:
            return HB_SCRIPT_MAKASAR;
        case SBScriptMEDF:
            return HB_SCRIPT_MEDEFAIDRIN;
        case SBScriptROHG:
            return HB_SCRIPT_HANIFI_ROHINGYA;
        case SBScriptSOGD:
            return HB_SCRIPT_SOGDIAN;
        case SBScriptSOGO:
            return HB_SCRIPT_OLD_SOGDIAN;

        /* Unicode 12.0 */
        case SBScriptELYM:
            return HB_SCRIPT_ELYMAIC;
        case SBScriptHMNP:
            return HB_SCRIPT_NYIAKENG_PUACHUE_HMONG;
        case SBScriptNAND:
            return HB_SCRIPT_NANDINAGARI;
        case SBScriptWCHO:
            return HB_SCRIPT_WANCHO;

        /* Unicode 13.0 */
        case SBScriptCHRS:
            return HB_SCRIPT_CHORASMIAN;
        case SBScriptDIAK:
            return HB_SCRIPT_DIVES_AKURU;
        case SBScriptKITS:
            return HB_SCRIPT_KHITAN_SMALL_SCRIPT;
        case SBScriptYEZI:
            return HB_SCRIPT_YEZIDI;

        /* Unicode 14.0 */
        case SBScriptCPMN:
            return HB_SCRIPT_CYPRO_MINOAN;
        case SBScriptOUGR:
            return HB_SCRIPT_OLD_UYGHUR;
        case SBScriptTNSA:
            return HB_SCRIPT_TANGSA;
        case SBScriptTOTO:
            return HB_SCRIPT_TOTO;
        case SBScriptVITH:
            return HB_SCRIPT_VITHKUQI;

        /* Unicode 15.1 */
        case SBScriptKAWI:
            return HB_SCRIPT_KAWI;
        case SBScriptNAGM:
            return HB_SCRIPT_NAG_MUNDARI;

        /* Unicode 16.0 */
        case SBScriptGARA:
            return HB_SCRIPT_GARAY;
        case SBScriptGUKH:
            return HB_SCRIPT_GURUNG_KHEMA;
        case SBScriptKRAI:
            return HB_SCRIPT_KIRAT_RAI;
        case SBScriptONAO:
            return HB_SCRIPT_OL_ONAL;
        case SBScriptSUNU:
            return HB_SCRIPT_SUNUWAR;
        case SBScriptTODR:
            return HB_SCRIPT_TODHRI;
        case SBScriptTUTG:
            return HB_SCRIPT_TULU_TIGALARI;

        /* Unicode 17.0 */
        case SBScriptBERF:
            return HB_SCRIPT_BERIA_ERFE;
        case SBScriptSIDT:
            return HB_SCRIPT_SIDETIC;
        case SBScriptTAYO:
            return HB_SCRIPT_TAI_YO;
        case SBScriptTOLS:
            return HB_SCRIPT_TOLONG_SIKI;

        default:
            return HB_SCRIPT_INVALID;
        }
    }
}; // namespace mcs::vulkan::font::bidi