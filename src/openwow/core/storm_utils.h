
#pragma once

#include <cstdint>
#include <string>

namespace openwow::core {

const char* SFileOpenFile_FindSubstringNoCaseIfNonNull(const char* haystack,
                                                       const char* needle,
                                                       uint32_t max_len);

void SErrAssertHandler_NormalizePath(const char* input, char* output,
                                      uint32_t output_size);

void* SFileOpenFileEx_CheckMagic(void* handle);

bool SFileOpenFileEx_ValidatedDelegate(void* handle, void* param);

int StormUtf16ToUtf8Bounded(char* output, uint32_t output_size,
                            const char16_t* input, int input_length,
                            uint32_t* bytes_written,
                            int* code_units_consumed);

int StormUtf8ToUtf16Bounded(char16_t* output, int output_length,
                            const char* input, int input_length,
                            int* code_units_written,
                            uint32_t* bytes_consumed);

int CurrentCodePageToUtf8Bounded(char* output, uint32_t output_size,
                                 const char* input);

std::string CurrentCodePageToUtf8String(const char* input);

std::string Utf8ToCurrentCodePageString(const char* input);

bool DeleteStormFilePath(const char* input);

bool MoveStormFilePathNoReplace(const char* source, const char* destination);

bool QueryStormPathFreeBytesAvailable(const char* path,
                                      std::uint64_t* out_free_bytes);

bool GetExeDirectory(char* output, uint32_t output_size);

namespace detail {

using GetExeDirectoryFullPathProvider = std::string (*)();

void SetGetExeDirectoryFullPathProviderForTests(
    GetExeDirectoryFullPathProvider provider);
void ResetGetExeDirectoryFullPathProviderForTests();

}

}
