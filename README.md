# Lajkonik

Lajkonik is a computer program that plays the board game Havannah. It was written in C++ by Marcin Ciura and Piotr Wieczorek. Among others, it incorporates ideas by Łukasz Lew, Timo Ewalds, Adam Byrtek, Witek Jarnicki, and Przemek Drochomirecki. Lajkonik won a bronze medal at the 15th Computer Olympiad in Kanazawa, Japan in 2010, and a silver medal at the 16th Olympiad in Tilburg, the Netherlands in 2011. Its name comes from one of symbols of Krakow, Poland where the authors live: a man disguised as a Mongolian warrior riding a hobbyhorse who dances through the streets beating passers-by with his mace.

A technical description of the source code of Lajkonik follows.

## 10000-foot view

The **Frontend** class (defined in _lajkonik.cc_) interprets commands in extended Go Text Protocol, given through the standard input or the /exec? URL endpoint, and calls appropriate methods of **Controller**.

**Controller** (defined in _controller.cc_) maintains the current **Position** and **Player** of the game. When Lajkonik is to move, **Controller** launches one or more threads of **MctsEngine** (MCTS stands for Monte-Carlo Tree Search). Then it repeats in a loop one-second sleeping and waking up for a moment. This loop ends when the allotted time is up, when the **MctsEngine** solves the given position as a forced win/loss/draw, or when it solves all of its child positions except one as forced wins for the opponent. Then **Controller** joins the threads and returns the selected move.

Each **MctsEngine** (defined in _mcts.cc_) contains its own **Position** and **Player** , a pointer to its own **Playout** , and a pointer to a **TranspositionTable** shared between threads. **MctsEngine** copies the private instances of **Position** and **Player** from the **Controller** , then modifies them while recursively descending the directed acyclic graph of positions from the root to a leaf, and finally passes them to **Playout::Play()**. After **Playout::Play()** returns, **MctsEngine** updates the elements of the **TranspositionTable** from the leaf to the root. Then, unless the **Controller** has ordered it to quit, it repeats the entire loop from the copying of **Position** and **Player**.

**Controller** , **MctsEngine** , and **Playout** have various parameters, declared as structs in _options.h_, filled with default values in _lajkonik.cc_, and modifiable at runtime via the set\_option command of Go Text Protocol.

## Helpers

Files _base.h_ and _base.cc_ define a few general-purpose macros and functions.

Class **Rng** (defined in _rng.h_) implements George Marsaglia’s ultra-fast XorShift random-number generator. In addition to generating random integers in the 0...N - 1 range, methods of **Rng** can shuffle a vector and pick its random element.

## Position

Everything defined in _havannah.cc_ has to do with the game mechanics of Havannah. The following subsections describe:

- Plain enums, introduced for clarity and type safety, or even simpler typedefs introduced for clarity only;
- Essential classes that build on top of each other, culminating in class **Position** ;
- Other necessary classes and one macro that do not fit into the above categories.

### POD types

The **Player** type can assume two values: **kWhite** and **kBlack**. The **Opponent()** function returns the other **Player**.

Values of the **WinningCondition** type are bit masks, equal either to **kNoWinningCondition** or to a bitwise OR of possible values: **kRing** , **kBenzeneRing** , **kBridge** , or **kFork**. **kRing** | **kBenzeneRing** is contained in the **WinningCondition** for rings made up of six stones, possibly with a seventh stone inside. The rules of Havannah do not require us to detect if a **kRing** is also a **kBenzeneRing** ; this is done for statistical purposes only.

The **XCoord** and **YCoord** types index two-dimensional arrays with 32 columns and **kBoardHeight** rows. The arrays reflect the hexagonal board as below. The sentinels on their sides simplify the extraction of cells nearby a given cell.

```
. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
. . . . . # # # # . . . . . . . . . . . . . . . . . . . . . . .
. . . . # # # # # . . . . . . . . . . . . . . . . . . . . . . .
. . . # # # # # # . . . . . . . . . . . . . . . . . . . . . . .
. . # # # # # # # . . . . . . . . . . . . . . . . . . . . . . .
. . # # # # # # . . . . . . . . . . . . . . . . . . . . . . . .
. . # # # # # . . . . . . . . . . . . . . . . . . . . . . . . .
. . # # # # . . . . . . . . . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .
```

The **Cell** type indexes one-dimensional versions of the above-mentioned arrays. Such arrays have **kNumCellsWithSentinels** == 32 \* **kBoardHeight** elements. The **XYToCell()** , **CellToX()** , and **CellToY()** functions convert between **XCoord** , **YCoord** , and **Cell**.

The **MoveIndex** (0... **kNumMovesOnBoard** - 1) type indexes dense arrays of cells. **kNumMovesOnBoard** is the number of hashes in the figure above, equal to 3 \* **SIDE\_LENGTH** \* ( **SIDE\_LENGTH** - 1). The mutually converse **Position::MoveIndexToCell()** and **Position::CellToMoveIndex()** methods convert between **MoveIndex** and **Cell** [_by applying a shuffled mapping. This shuffling prevents Lajkonik from preferring cells with low indices during Monte-Carlo Tree Search. A better approach might be to add noise to cells’ priors or bias. A rarely used static method of **Position** applies a non-shuffled mapping._] TODO(marcinc): Replace the shuffling with random priors.

**ChainNum** is a typedef for **unsigned char**. It indexes the elements of a **ChainSet**. The smaller the type is, the better an array with **kNumCellsWithSentinels**  **ChainNum** elements fits into the cache.

**RowBitmask** is a typedef for **unsigned int**. It marks cells in one row of the board.

**Hash** is a typedef for **unsigned long long**. It is used for Zobrist hashing, in which the **Hash** of a position is a bitwise XOR of **Hashes** of all its moves.

**CoordinateSystem** is an enum with two values. The global variable **g\_coordinate\_system** influences all conversions to and from strings.

### Essential classes

The **BoardBitmask** class wraps an array of **kBoardHeight**  **RowBitmasks**. It is used in **Chain** , **PlayerPosition** , and **Position** to mark some cells.

Class **Chain** represents a connected group of stones of one player. It consists of the following fields:

- a **BoardBitmask** with a set bit wherever a cell belongs to this **Chain** ;
- a (6+6+2)-bit mask of six edges, six corners, a ring, and a benzene ring that this **Chain** touches or forms;
- a **ChainNum** of a newer version of this **Chain** (to get an up-to-date version of the **Chain** , we have to walk a linked list of **Chains** until we find a **Chain** whose newer version has index zero).

A freshly created **Chain** contains one stone. The **ChainNum** field denoting its newer version is initialized to zero. When a new stone is added to a **Chain** , only the **BoardBitmask** and perhaps the edge-corner-ring mask of the **Chain** changes – unless the stone joins two **Chains**. In that case, illustrated below, a new **Chain** is created. Its **BoardBitmask** and edge-corner-ring mask are set to the union of respective fields from both **Chains** ; the **ChainNum** denoting its newer version is set to zero. Then, in both joined **Chains** , their newer version index is set to the index of the new **Chain**. This constitutes a poor man’s variant of the union-find algorithm. Its advantage lies in the ability to undo changes fast.

```
               Before a move:
PlayerPosition  Chain 1->0    Chain 2->0
    . . .           . . .         . . .
  . 2 2 2         . . . .       . # # #
. . . . .       . . . . .     . . . . .
. 1 1 1         . # # #       . . . .
. . .           . . .         . . .

                After a move:
PlayerPosition  Chain 1->3    Chain 2->3    Chain 3->0
    . . .           . . .         . . .         . . .
  . 2 2 2         . . . .       . # # #       . # # #
. . . 1 .       . . . # .     . . . . .     . . . # .
. 1 1 1         . # # #       . . . .       . # # #
. . .           . . .         . . .         . . .
```

Adding every stone entails an O(1) recognition of **WinningCondition**. This is achieved by counting appropriate set bits in the edge-corner mask and by a lookup in a static array indexed by six bits corresponding to six cells adjacent to the affected cell.

Class **ChainSet** wraps a vector of **Chain** pointers. The unused element at index zero is a **NULL** pointer.

The fields of class **PlayerPosition** are:

- its **ChainSet** ;
- an array of **kNumCellsWithSentinels** elements that maps empty cells to zeroes and each stone of this player to the **ChainNum** of some version of its **Chain** within the **ChainSet** (to obtain its latest version, we have to call **ChainSet::GetNewestVersion()** );
- a **BoardBitmask** of all stones that this player has put on the board.

Static fields of class **Position** include:

- an array of **kNumCellsWithSentinels** (6+6)-bit masks that have appropriate bits set on edges and corners of the board;
- a [_non-shuffled_] mapping of move indices to cells;
- an array of **Hash** values for each ( **MoveIndex** , **Player** ) combination used for Zobrist hashing;
- a **BoardBitmask** that distinguishes cells on the board from cells outside the board.

The following are non-static fields of class **Position** :

- a Boolean **is\_initialized** ;
- [_shuffled_] two-way mappings between **MoveIndex** and **Cell** ;
- an integer **num\_available\_moves** that gets decremented with each stone permanently put on the board; in the above-mentioned shuffled mapping, the elements [0... **num\_available\_moves** - 1] point to cells that were empty after the last permanent move (but some of them may contain temporarily put stones); the elements [**num\_available\_moves**... **kNumMovesOnBoard** - 1] point to cells that have been occupied during the previous moves of the game.
- **cells\_** : an array of **kNumCellsWithSentinels**  **unsigned chars** that contains 0 for empty cells, **kWhite** + 1 == 1 or **kBlack** + 1 == 2 for cells occupied by players’ stones, or 3 for cells outside the board;
- a vector of pointers to **Mementoes** that are able to revert successive moves made during the game;
- two **PlayerPositions**.

The constructor of **Position** does little work. Every **Position** should be initialized by calling **InitToStartPosition()** , **CopyFrom()** , or **FullCopyFrom()**. **CopyFrom()** only copies **cells\_** and the **PlayerPositions** ; **FullCopyFrom()** also copies **num\_available\_moves** and the two-way mappings (it still does not copy the vector of **Mementoes** ). The **MakeMoveReversibly()** , **MakeMoveFast()** , and **MakePermanentMove()** methods of **Position** put a stone of the given player in the given cell and return a **WinningCondition**. The **MoveIsWinning()** method does not change the Position and returns a bool.

### Auxiliaries

Class **PrintableBoard** is an abstract base class for classes that represent boards and are convertible to strings.

Class **ChainAllocator** is a freelist allocator of **Chains**. It decreases the memory management contention in multithreaded versions of Lajkonik. Since each **Chain** has the same size, the code is simple. Each **ChainSet** has its own **ChainAllocator**.

Class **Memento** remembers changes to 32-bit or 8-bit unsigned integers located in memory and to sizes of **ChainSets**. The **Memento::UndoAll()** method reverts all the changes, in the order from the most recent one to the oldest one. Many methods that modify memory have **...Reversibly()** and **...Fast()** versions. The **...Reversibly()** versions use **Memento** , unlike the **...Fast()** ones.

The **RepeatForCellsAdjacentToChain()** macro calls the given function once for every cell adjacent to the **Chain** the given cell belongs to. The given cell must contain a stone of the given player.

## Patterns

The shape (as opposed to content) of all patterns recognized by Lajkonik is symmetrical. In their center lies the cell where the last move was made. The largest ones comprise its 18 neighbours; smaller – 12 neighbours; the smallest ones – 6 neighbours. Trying out 30-cell patterns is on the TODO list. The shape of the mentioned varieties is shown below.

```
                                         * *
   * * *          *                   * * * * *
  * * * *      * * * *      * *      * * * * * *
 * *   * *      *   *      *   *      * *   * *
  * * * *      * * * *      * *      * * * * * *
   * * *          *                   * * * * *
                                         * *
```

Patterns returned by **Position::Get18Neighbors()** have 36 bits since each of 18 cells they comprise is in one of four states:

- empty,
- occupied by a stone of the player who made the last move,
- occupied by a stone of the player to move,
- outside the board.

Given such a 36-bit pattern, **Patterns::GetMoveSuggestion()** returns a **MoveSuggestion** object composed of an 18-bit mask of moves and of 'chance' (a number from 0 to 8). The object is looked up in the inner **PatternHashMap**. The lookup uses the given pattern or its subpatterns trimmed to 12 or 6 cells. If none of these three patterns is present, both fields of the returned **MoveSuggestion** are zeroed out. Methods of **MoveSuggestion** return the index of a randomly chosen set bit in the mask and determine whether the pattern should be followed with probability (chance / 8).

The constructor of **Patterns** compiles the contents of their inner **PatternHashMap** (a different entity from **TranspositionTable** ; has nothing to do with **Hash** ) from an array of **StringPatterns**. Each **StringPattern** begets up to 12 mappings from **unsigned long long** to **MoveSuggestion** that correspond to all its rotations and mirror images. A **StringPattern** is a triple composed of:

- a **char\*** schema of the contents of 6, 12, or 18 nearby cells, in the form "abcdef", "abcdef/ghijkl", or "abcdef/ghijkl/mnopqr", with each letter substituted by ' **.**', ' **x**', ' **o**', or ' **#**', according to the four cases above – this is compiled to the **unsigned long long** key;
- a **char\*** schema of recommended moves into one or more among 18 nearby cells, in the form "abcdef/ghijkl/mnopqr", with each letter substituted by ' **.**' ("don't move here") or ' **o**' ("can move here") – this is compiled to **MoveSuggestion::mask** ;
- a number between 0 and 8, determining the probability of following the recommendation – this is compiled to **MoveSuggestion::chance**.

Letters in the above schemata correspond to the nearby cells as follows:

```
   o h n
  i c b g
 p d   a m
  j e f l
   q k r
```
