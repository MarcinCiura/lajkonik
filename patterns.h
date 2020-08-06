#ifndef PATTERNS_H_
#define PATTERNS_H_

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

// Declaration of classes concerned with suggesting moves based on patterns.

#include <assert.h>
#include <stddef.h>

#include "base.h"
#include "rng.h"

namespace lajkonik {

// Maps keys to Elements. Resolves collisions by quadratic probing.
template<typename Element>
class PatternHashMap {
 public:
  PatternHashMap(int capacity)
      : mask_(capacity - 1),
        array_(new Element[capacity]) {
    // Fail if capacity is not a power of two.
    assert((capacity & (capacity - 1)) == 0);
  }

  ~PatternHashMap() {
    delete[] array_;
  }

  // Inserts element. Returns the number of collisions on the way.
  // An assertion fails upon a second attempt to insert an element
  // with the same key.
  int Insert(Element element) {
    int hash = Hash(element.key) & mask_;
    int collisions = 0;
    while (array_[hash].key != kEmptyKey) {
      assert(array_[hash].key != element.key);
      ++collisions;
      hash = (hash + collisions) & mask_;
    }
    array_[hash] = element;
    return collisions;
  }

  // Returns a pointer to an element with the given key
  // or NULL if there is no such element.
  const Element* Find(unsigned long long key) const {
    int hash = Hash(key) & mask_;
    int collisions = 0;
    while (array_[hash].key != key) {
      if (array_[hash].key == kEmptyKey)
        return NULL;
      ++collisions;
      hash = (hash + collisions) & mask_;
    }
    return &array_[hash];
  }

 private:
  // A 64-bit hash function, taken from the finalizer of MurmurHash3.
  static int Hash(unsigned long long key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return key;
  }

  // Marks unoccupied entries of array_.
  static const unsigned long long kEmptyKey = 0ULL;

  // One less than the size of array_.
  const int mask_;
  // The underlying array of Elements. The size must be a power of two.
  Element* array_;

  PatternHashMap(const PatternHashMap&);
  void operator=(const PatternHashMap&);
};

// An element of arrays fed to the constructor of Patterns.
// Letters in the strings correspond to the neighboring cells as follows:
//
//    o h n
//   i c b g
//  p d   a m
//   j e f l
//    q k r
//
// plus all rotations and mirror images.
struct StringPattern {
  // Contents of 6, 12, or 18 neighboring cells, in the form "abcdef",
  // "abcdef/ghijkl", or "abcdef/ghijkl/mnopqr", with each letter substituted
  // by one of:
  //  '.' for empty cells;
  //  'x' for cells occupied by a stone of the player who made the last move;
  //  'o' for cells occupied by a stone of the player to move;
  //  '#' for cells outside the board.
  const char* neighbors;
  // Recommended moves into one or more among 18 neighboring cells,
  // in the form "abcdef/ghijkl/mnopqr", withc each letter substituted by:
  //  '.' ("don't move here");
  //  'o' ("can move here").
  const char* mask;
  // A number between 0 and 8, determining the probability of following
  // the recommendation (from 0/8 to 8/8).
  unsigned chance;
};

// An 18-neighbor mask of suggested replies with 4 bits of additional
// information for calculating the chance of making the replies.
// TODO(marcinc): Try 30-neighbor instead of 18-neighbor masks.
struct MoveSuggestion {
  MoveSuggestion()
      : mask(0), chance(0) {}
  MoveSuggestion(unsigned m, unsigned c)
      : mask(m), chance(c) {}

  // Returns true if this pattern should be used.
  bool ChancesAreAuspicious(Rng* rng) const {
    return (chance > static_cast<unsigned>((*rng)(8)));
  }

  // Returns the index of a randomly chosen bit of mask.
  int GetIndexOfRandomBitOfMask(Rng* rng) const {
    const unsigned m = mask;
    assert(m != 0);
    const int n =
        CountSetBits(m) + CountSetBits(m >> 6) + CountSetBits(m >> 12);
    if (n == 1)
      return GetIndexOfNthBit(0, m);
    else
      return GetIndexOfNthBit((*rng)(n), m);
  }

  unsigned mask: 18;
  unsigned chance: 4;
};

// An element of the pattern hash map: a 36-bit key and its MoveSuggestion.
struct Element {
  Element()
      : key(0), mask(0), chance(0) {}
  Element(unsigned long long k, MoveSuggestion ms)
      : key(k), mask(ms.mask), chance(ms.chance) {}

  unsigned long long key: 36;
  unsigned mask: 18;
  unsigned chance: 4;
};

// TODO.
class Patterns {
 public:
  // Takes an array of StringPatterns, terminated by a StringPattern
  // with neighbors=NULL. For each StringPattern, inserts all its rotations
  // and mirror images into an internal PatternHashMap. When StringPatterns
  // exhibit rotational or axial symmetry, ORs the masks for matching keys
  // prior to the insertion.
  Patterns(const StringPattern string_patterns[], double max_load = 0.667);
  ~Patterns();

  // Accepts a 36-bit mask of 18 neighbors of a given cell, as returned
  // by Position::Get18Neighbors. If the mask, or the mask trimmed
  // to the closest 12 neighbors, or the mask trimmed to the closest
  // 6 neighbors is a key of a known pattern, returns the found
  // MoveSuggestion. Otherwise, returns a zeroed out MoveSuggestion.
  MoveSuggestion GetMoveSuggestion(unsigned long long neighbors18) const;

 private:
  // The underlying PatternHashMap.
  PatternHashMap<Element>* hash_map_;

  Patterns(const Patterns&);
  void operator=(const Patterns&);
};

// TODO.
std::string NeighborsToString(unsigned long long neighbors18);

}  // namespace lajkonik

#endif  // PATTERNS_H_
