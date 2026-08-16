#pragma once

#include <cstdint>

namespace openwow::ui::xml {

namespace bt {
constexpr int kNonXml  = 0;
constexpr int kMalform = 1;
constexpr int kLt      = 2;
constexpr int kAmp     = 3;
constexpr int kRsqb    = 4;
constexpr int kLead2   = 5;
constexpr int kLead3   = 6;
constexpr int kLead4   = 7;
constexpr int kTrail   = 8;
constexpr int kCr      = 9;
constexpr int kLf      = 10;
constexpr int kGt      = 11;
constexpr int kQuot    = 12;
constexpr int kApos    = 13;
constexpr int kEquals  = 14;
constexpr int kQuest   = 15;
constexpr int kExcl    = 16;
constexpr int kSol     = 17;
constexpr int kSemi    = 18;
constexpr int kNum     = 19;
constexpr int kLsqb    = 20;
constexpr int kS       = 21;
constexpr int kNmstrt  = 22;
constexpr int kColon   = 23;
constexpr int kHex     = 24;
constexpr int kDigit   = 25;
constexpr int kName    = 26;
constexpr int kMinus   = 27;
constexpr int kOther   = 28;
constexpr int kNonAscii = 29;
constexpr int kPercnt  = 30;
}

namespace xml_tok {
constexpr int kNone             = -4;
constexpr int kTrailingCr       = -3;
constexpr int kPartialChar      = -2;
constexpr int kPartial          = -1;
constexpr int kInvalid          =  0;
constexpr int kDataChars        =  6;
constexpr int kDataNewline      =  7;
constexpr int kEntityRef        =  9;
constexpr int kCharRef          = 10;
constexpr int kAttributeValueS  = 39;
}

constexpr int kEncodingTypeTableOffset = 76;

int big2_charType(unsigned char hi, unsigned char lo);

int big2_scanHexCharRef(const void* enc, const char* ptr,
                        const char* end, const char** nextTokPtr);

int big2_scanCharRef(const void* enc, const char* ptr,
                     const char* end, const char** nextTokPtr);

int big2_scanRef(const void* enc, const char* ptr,
                 const char* end, const char** nextTokPtr);

int big2_attributeValueTok(const void* enc, const char* ptr,
                           const char* end, const char** nextTokPtr);

}
