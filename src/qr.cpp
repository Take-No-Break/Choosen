#include "qr.h"

#include <algorithm>
#include <cstdlib>

namespace {

// Error correction level M: codewords per block and number of blocks, by version (index 1-6).
const int kEccPerBlock[7] = {0, 10, 16, 26, 18, 24, 16};
const int kNumBlocks[7] = {0, 1, 1, 1, 2, 2, 4};

// Modules available for data and error correction (everything but the function patterns).
int rawDataModules(int ver) {
  int r = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    const int numAlign = ver / 7 + 2;
    r -= (25 * numAlign - 10) * numAlign - 55;
  }
  return r;
}

int dataCodewords(int ver) { return rawDataModules(ver) / 8 - kEccPerBlock[ver] * kNumBlocks[ver]; }

// Multiplication in GF(2^8) modulo x^8 + x^4 + x^3 + x^2 + 1.
uint8_t gfMul(uint8_t x, uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return (uint8_t)z;
}

// Generator polynomial (x - a^0)(x - a^1)...(x - a^(degree-1)), leading 1 omitted.
std::vector<uint8_t> rsDivisor(int degree) {
  std::vector<uint8_t> r(degree, 0);
  r[degree - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      r[j] = gfMul(r[j], root);
      if (j + 1 < degree) r[j] ^= r[j + 1];
    }
    root = gfMul(root, 0x02);
  }
  return r;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
  std::vector<uint8_t> r(divisor.size(), 0);
  for (uint8_t b : data) {
    const uint8_t factor = b ^ r[0];
    r.erase(r.begin());
    r.push_back(0);
    for (size_t i = 0; i < r.size(); ++i) r[i] ^= gfMul(divisor[i], factor);
  }
  return r;
}

bool maskBit(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

struct Symbol {
  int n;
  std::vector<std::vector<char>> dark, fixed;  // [y][x]; fixed = function pattern (never masked)
  explicit Symbol(int size) : n(size), dark(size, std::vector<char>(size, 0)), fixed(size, std::vector<char>(size, 0)) {}
  void set(int x, int y, bool d) {
    dark[y][x] = d;
    fixed[y][x] = 1;
  }

  void formatBits(int mask) {
    const int bits = qrFormatBits(mask);
    auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) set(8, i, bit(i));
    set(8, 7, bit(6));
    set(8, 8, bit(7));
    set(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) set(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) set(n - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) set(8, n - 15 + i, bit(i));
    set(8, n - 8, true);  // the dark module
  }

  void applyMask(int mask) {
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x)
        if (!fixed[y][x] && maskBit(mask, x, y)) dark[y][x] ^= 1;
  }

  // Penalty rules of the standard: runs of 5+, 2x2 blocks, finder-like 1:1:3:1:1 patterns, balance.
  long penalty() const {
    long p = 0;
    auto line = [&](bool rows, int i, int j) { return rows ? dark[i][j] : dark[j][i]; };
    for (int r = 0; r < 2; ++r) {
      const bool rows = r == 0;
      for (int i = 0; i < n; ++i) {
        int run = 1;
        for (int j = 1; j <= n; ++j) {
          if (j < n && line(rows, i, j) == line(rows, i, j - 1)) {
            ++run;
          } else {
            if (run >= 5) p += 3 + (run - 5);
            run = 1;
          }
        }
        std::string s;
        for (int j = 0; j < n; ++j) s += line(rows, i, j) ? '1' : '0';
        for (const char* pat : {"10111010000", "00001011101"})
          for (size_t at = s.find(pat); at != std::string::npos; at = s.find(pat, at + 1)) p += 40;
      }
    }
    for (int y = 0; y + 1 < n; ++y)
      for (int x = 0; x + 1 < n; ++x)
        if (dark[y][x] == dark[y][x + 1] && dark[y][x] == dark[y + 1][x] && dark[y][x] == dark[y + 1][x + 1]) p += 3;
    long darkCount = 0;
    for (auto& row : dark)
      for (char c : row) darkCount += c;
    // the smallest k >= 0 with (45 - 5k)% <= dark share <= (55 + 5k)% (n is odd: never exactly half)
    const long total = (long)n * n;
    p += 10 * ((std::abs(darkCount * 20 - total * 10) + total - 1) / total - 1);
    return p;
  }
};

}  // namespace

int qrFormatBits(int mask) {
  const int data = (0 << 3) | mask;  // level M = 00
  int rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  return ((data << 10) | rem) ^ 0x5412;
}

std::vector<uint8_t> qrErrorCorrection(const std::vector<uint8_t>& data, int eccLen) {
  return rsRemainder(data, rsDivisor(eccLen));
}

std::vector<std::string> qrEncode(const std::string& text) {
  int ver = 1;
  while (ver <= 6 && (int)text.size() * 8 + 12 > dataCodewords(ver) * 8) ++ver;
  if (ver > 6) return {};

  // data: byte mode, length, the bytes, terminator, padding
  const int capacity = dataCodewords(ver) * 8;
  std::vector<char> bits;
  auto put = [&](unsigned v, int count) {
    for (int i = count - 1; i >= 0; --i) bits.push_back((v >> i) & 1);
  };
  put(4, 4);
  put((unsigned)text.size(), 8);
  for (unsigned char c : text) put(c, 8);
  put(0, std::min(4, capacity - (int)bits.size()));
  put(0, (8 - (int)bits.size() % 8) % 8);
  for (unsigned pad = 0xEC; (int)bits.size() < capacity; pad ^= 0xEC ^ 0x11) put(pad, 8);
  std::vector<uint8_t> data(bits.size() / 8, 0);
  for (size_t i = 0; i < bits.size(); ++i) data[i >> 3] |= (uint8_t)(bits[i] << (7 - (i & 7)));

  // split into blocks, add error correction, interleave
  const int numBlocks = kNumBlocks[ver], eccLen = kEccPerBlock[ver];
  const int rawCodewords = rawDataModules(ver) / 8;
  const int numShort = numBlocks - rawCodewords % numBlocks;
  const int shortLen = rawCodewords / numBlocks;
  const std::vector<uint8_t> divisor = rsDivisor(eccLen);
  std::vector<std::vector<uint8_t>> blocks;
  for (int i = 0, k = 0; i < numBlocks; ++i) {
    const int len = shortLen - eccLen + (i < numShort ? 0 : 1);
    std::vector<uint8_t> block(data.begin() + k, data.begin() + k + len);
    k += len;
    const std::vector<uint8_t> ecc = rsRemainder(block, divisor);
    if (i < numShort) block.push_back(0);  // keeps the columns aligned; skipped when interleaving
    block.insert(block.end(), ecc.begin(), ecc.end());
    blocks.push_back(block);
  }
  std::vector<uint8_t> all;
  for (size_t i = 0; i < blocks[0].size(); ++i)
    for (int j = 0; j < numBlocks; ++j)
      if (i != (size_t)(shortLen - eccLen) || j >= numShort) all.push_back(blocks[j][i]);

  // function patterns
  const int n = ver * 4 + 17;
  Symbol s(n);
  for (int i = 0; i < n; ++i) {
    s.set(6, i, i % 2 == 0);
    s.set(i, 6, i % 2 == 0);
  }
  auto finder = [&](int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int x = cx + dx, y = cy + dy, d = std::max(std::abs(dx), std::abs(dy));
        if (x >= 0 && x < n && y >= 0 && y < n) s.set(x, y, d != 2 && d != 4);
      }
  };
  finder(3, 3);
  finder(n - 4, 3);
  finder(3, n - 4);
  if (ver >= 2) {  // versions 2-6 have one alignment pattern, centred at (n-7, n-7)
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) s.set(n - 7 + dx, n - 7 + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
  }
  s.formatBits(0);  // reserves the format areas

  // data in the zigzag order of the standard, two columns at a time from the bottom right
  size_t bit = 0;
  for (int right = n - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < n; ++vert)
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? n - 1 - vert : vert;
        if (!s.fixed[y][x] && bit < all.size() * 8) {
          s.dark[y][x] = (all[bit >> 3] >> (7 - (bit & 7))) & 1;
          ++bit;
        }
      }
  }

  // the mask with the lowest penalty
  int best = 0;
  long bestPenalty = -1;
  for (int mask = 0; mask < 8; ++mask) {
    s.applyMask(mask);
    s.formatBits(mask);
    const long p = s.penalty();
    if (bestPenalty < 0 || p < bestPenalty) {
      bestPenalty = p;
      best = mask;
    }
    s.applyMask(mask);  // undo
  }
  s.applyMask(best);
  s.formatBits(best);

  std::vector<std::string> rows;
  for (int y = 0; y < n; ++y) {
    std::string r;
    for (int x = 0; x < n; ++x) r += s.dark[y][x] ? '1' : '0';
    rows.push_back(r);
  }
  return rows;
}
