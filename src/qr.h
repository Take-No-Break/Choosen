// QR Code encoder (ISO/IEC 18004) for short texts such as the link that pairs a phone: byte mode,
// error correction level M, versions 1-6 (up to 106 bytes). No external library.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The symbol as rows of '1' (dark) and '0' (light), without the quiet zone; {} if text is too long.
std::vector<std::string> qrEncode(const std::string& text);

// Reed-Solomon error correction codewords of `data` (for the self-test).
std::vector<uint8_t> qrErrorCorrection(const std::vector<uint8_t>& data, int eccLen);

// The 15 format bits for error correction level M and a mask (for the self-test).
int qrFormatBits(int mask);
