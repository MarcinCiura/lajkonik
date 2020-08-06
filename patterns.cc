// Copyright (c) 2012 Marcin Ciura, Piotr Wieczorek
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Definition of the PlayoutPattern class.

#include "patterns.h"

#include <assert.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

namespace lajkonik {
namespace {

const unsigned long long kOrTo12Neighbors =
    ~(~0ULL << 36) & ~(((1ULL << 18) + 1ULL) * kAndTo12Neighbors);
const unsigned long long kOrTo6Neighbors =
    ~(~0ULL << 36) & ~(((1ULL << 18) + 1ULL) * kAndTo6Neighbors);

const int kKeyIndices[6 + 1 + 6 + 1 + 6] = {
  8, 12, 13, 9, 5, 4, -1,
  11, 16, 14, 6, 1, 3, -1,
  7, 15, 17, 10, 2, 0,
};

unsigned long long ToKey(const std::string& neighbors18) {
  unsigned long long key = 0;
  for (int i = 0, size = neighbors18.size(); i < size; ++i) {
    unsigned long long multiplier;
    switch (neighbors18[i]) {
      case '/':
      case '.':
        continue;
      case 'o':
        multiplier = 1ULL;
        break;
      case 'x':
        multiplier = (1ULL << 18);
        break;
      case '#':
        multiplier = (1ULL << 18) + 1ULL;
        break;
      default:
        assert(false);
        continue;
    }
    key += (multiplier << kKeyIndices[i]);
  }
  return key;
}

}  // namespace

std::string NeighborsToString(unsigned long long neighbors18) {
  std::string result = "....../....../......";
  for (int i = 0; i < 18; ++i) {
    int j;
    for (j = 0; j < ARRAYSIZE(kKeyIndices); ++j) {
      if (kKeyIndices[j] == i)
        break;
    }
    const unsigned long long mask1 = (1ULL << i);
    const unsigned long long mask2 = (1ULL << (i + 18));
    if (neighbors18 & mask1) {
      if (neighbors18 & mask2) {
        result[j] = '#';
      } else {
        result[j] = 'o';
      }
    } else {
      if (neighbors18 & mask2) {
        result[j] = 'x';
      } else {
        result[j] = '.';
      }
    }
  }
  return result;
}

namespace {

unsigned ToValue(const std::string& mask18) {
  static const int kIndices[6 + 1 + 6 + 1 + 6] = {
    0, 1, 2, 3, 4, 5, -1,
    6, 7, 8, 9, 10, 11, -1,
    12, 13, 14, 15, 16, 17,
  };
  unsigned value = 0;
  for (int i = 0, size = mask18.size(); i < size; ++i) {
    switch (mask18[i]) {
      case '/':
      case '.':
        break;
      case 'o':
        value += (1 << kIndices[i]);
        break;
      default:
        assert(false);
        break;
    }
  }
  return value;
}

std::string Rotate6Chars1(const std::string& s6, int n) {
  assert(s6.size() == 6);
  assert(n >= 0);
  assert(n < 12);
  std::string result("123456");
  if (n < 6) {
    for (int i = 0; i < 6; ++i) {
      result[i] = s6[(i + n) % 6];
    }
  } else {
    for (int i = 0; i < 6; ++i) {
      result[5 - i] = s6[(i + n) % 6];
    }
  }
  return result;
}

std::string Rotate6Chars2(const std::string& s6, int n) {
  assert(s6.size() == 6);
  assert(n >= 0);
  assert(n < 12);
  std::string result("123456");
  if (n < 6) {
    for (int i = 0; i < 6; ++i) {
      result[i] = s6[(i + n) % 6];
    }
  } else {
    for (int i = 0; i < 6; ++i) {
      result[5 - i] = s6[(i + n + 5) % 6];
    }
  }
  return result;
}

std::string Rotate(const std::string& s18, int n) {
  std::string s6a = Rotate6Chars1(s18.substr(0, 6), n);
  if (s18.size() == 6)
    return s6a + "/######/######";
  std::string s6b = Rotate6Chars2(s18.substr(6 + 1, 6), n);
  if (s18.size() == 6 + 1 + 6)
    return s6a + '/' + s6b + "/######";
  std::string s6c = Rotate6Chars1(s18.substr(6 + 1 + 6 + 1, 6), n);
  assert(s18.size() == 6 + 1 + 6 + 1 + 6);
  return s6a + '/' + s6b + '/' + s6c;
}

}  // namespace

Patterns::Patterns(const StringPattern string_patterns[], double max_load) {
  assert(max_load > 0);
  assert(max_load <= 1);
  std::map<unsigned long long, MoveSuggestion> rotations;
  std::vector<Element> elements;
  for (int i = 0; string_patterns[i].neighbors != NULL; ++i) {
    rotations.clear();
    const std::string neighbors = string_patterns[i].neighbors;
    const std::string mask = string_patterns[i].mask;
    const unsigned chance = string_patterns[i].chance;
    for (int j = 0; j < 12; ++j) {
      const unsigned long long key = ToKey(Rotate(neighbors, j));
      const unsigned value = ToValue(Rotate(mask, j));
      rotations[key].mask |= value;
      rotations[key].chance = chance;
    }
    for (std::map<unsigned long long, MoveSuggestion>::const_iterator it =
         rotations.begin(); it != rotations.end(); ++it) {
      elements.push_back(Element(it->first, it->second));
    }
  }
  unsigned capacity;
  if (!elements.empty()) {
    // Round min_capacity up to the next highest power of 2.
    capacity = elements.size() / max_load - 1;
    capacity |= (capacity >> 1);
    capacity |= (capacity >> 2);
    capacity |= (capacity >> 4);
    capacity |= (capacity >> 8);
    capacity |= (capacity >> 16);
    ++capacity;
    assert(capacity > elements.size());
  } else {
    capacity = 1;
  }
  hash_map_ = new PatternHashMap<Element>(capacity);
  int collisions = 0;
  for (int i = 0, size = elements.size(); i < size; ++i) {
    collisions += hash_map_->Insert(elements[i]);
  }
  fprintf(stderr, "%zd patterns in %d buckets; %d collisions.\n",
          elements.size(), capacity, collisions);
}

Patterns::~Patterns() {
  delete hash_map_;
}

MoveSuggestion Patterns::GetMoveSuggestion(
    unsigned long long neighbors18) const {
  MoveSuggestion result;
  const Element* elem;
  elem = hash_map_->Find(neighbors18);
  if (elem != NULL) {
    result.mask = elem->mask;
    result.chance = elem->chance;
    return result;
  }
  elem = hash_map_->Find(neighbors18 | kOrTo12Neighbors);
  if (elem != NULL) {
    result.mask = elem->mask;
    result.chance = elem->chance;
    return result;
  }
  elem = hash_map_->Find(neighbors18 | kOrTo6Neighbors);
  if (elem != NULL) {
    result.mask = elem->mask;
    result.chance = elem->chance;
    return result;
  }
  return result;
}

}  // namespace lajkonik
