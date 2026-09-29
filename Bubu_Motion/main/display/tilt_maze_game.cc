#include "tilt_maze_game.h"

#include "settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include <esp_log.h>

namespace TiltMazeGame {
namespace {

constexpr char kLevel1Rows[][7] = {
    "######", "#....#", "#.##.#", "#....#", "#.##.#", "######",
};
constexpr const char* kLevel1Map[] = {
    kLevel1Rows[0], kLevel1Rows[1], kLevel1Rows[2],
    kLevel1Rows[3], kLevel1Rows[4], kLevel1Rows[5],
};
constexpr Cell kLevel1Gold[] = {{1, 2}, {3, 3}};

constexpr char kLevel2Rows[][11] = {
    "##########", "#...#....#", "#.#.#.##.#",
    "#.#...#..#", "#.....#..#", "##########",
};
constexpr const char* kLevel2Map[] = {
    kLevel2Rows[0], kLevel2Rows[1], kLevel2Rows[2],
    kLevel2Rows[3], kLevel2Rows[4], kLevel2Rows[5],
};
constexpr Cell kLevel2Gold[] = {{1, 1}, {3, 4}, {5, 2}, {8, 3}};

constexpr char kLevel3Rows[][12] = {
    "###########", "#...#.....#", "#.#.#.###.#", "#.#...#...#",
    "#.#####.#.#", "#.......#.#", "###########",
};
constexpr const char* kLevel3Map[] = {
    kLevel3Rows[0], kLevel3Rows[1], kLevel3Rows[2], kLevel3Rows[3],
    kLevel3Rows[4], kLevel3Rows[5], kLevel3Rows[6],
};
constexpr Cell kLevel3Gold[] = {{1, 2}, {3, 3}, {5, 1}, {7, 5}, {9, 3}};
constexpr WormholePair kLevel3Wormholes[] = {
    {{3, 5}, {5, 1}, 0, -10, 0, 10, 0},
};

constexpr char kLevel4Rows[][14] = {
    "#############", "#...#.......#", "#.#.#.###.#.#", "#.#...###.#.#",
    "#.#####.X...#", "#.......#.#.#", "#.#####.....#", "#############",
};
constexpr const char* kLevel4Map[] = {
    kLevel4Rows[0], kLevel4Rows[1], kLevel4Rows[2], kLevel4Rows[3],
    kLevel4Rows[4], kLevel4Rows[5], kLevel4Rows[6], kLevel4Rows[7],
};
constexpr Cell kLevel4Gold[] = {{1, 4}, {3, 1}, {3, 3}, {7, 5}, {9, 6}, {11, 2}};
constexpr WormholePair kLevel4Wormholes[] = {
    {{3, 5}, {3, 1}, 0, -10, 0, 10, 0},
    {{9, 6}, {1, 4}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel4Boosts[] = {{{7, 4}, 1, 0}};

constexpr const char* kLevel5Map[] = {
    "#############", "#.....#.....#", "#.###.#.###.#",
    "#.#.#.....#.#", "#.#.#######.#", "#...#...#...#",
    "#####...X.#.#", "#.....#...#.#", "#############",
};
constexpr Cell kLevel5Gold[] = {{5, 7}, {8, 7}, {11, 5}, {3, 3}, {2, 5}};
constexpr WormholePair kLevel5Wormholes[] = {
    {{5, 7}, {9, 6}, 0, -10, 10, 0, 0},
};
constexpr BoostPad kLevel5Boosts[] = {{{7, 6}, 1, 0}};

constexpr const char* kLevel6Map[] = {
    "#############", "#...#.......#", "#.###.#####.#",
    "#.....#...#.#", "#######...#.#", "#...#...#...#",
    "#.#.#.#####.#", "#.#...#.....#", "#############",
};
constexpr Cell kLevel6Gold[] = {{3, 5}, {5, 6}, {7, 4}, {11, 5}, {3, 1}, {1, 2}};
constexpr WormholePair kLevel6Wormholes[] = {
    {{3, 6}, {7, 4}, -10, 0, 0, 10, 0},
};

constexpr const char* kLevel7Map[] = {
    "###############", "#.....#.......#", "#.#####.#####.#",
    "#...........#.#", "#####.#######.#", "#...X.#.....#.#",
    "#.#.###.###...#", "#.#.....#.....#", "###############",
};
constexpr Cell kLevel7Gold[] = {{3, 6}, {7, 7}, {9, 5}, {13, 6}, {5, 1}, {2, 1}};
constexpr WormholePair kLevel7Wormholes[] = {
    {{3, 6}, {9, 5}, -10, 0, 0, 10, 0},
};
constexpr BoostPad kLevel7Boosts[] = {{{3, 5}, 1, 0}};

constexpr const char* kLevel8Map[] = {
    "###############", "#.#...........#", "#.#.#.#####.#.#",
    "#...#.....#...#", "#####.###.#.###", "#...X.#...#.#.#",
    "#...###.###.#.#", "#.#.....#.....#", "###############",
};
constexpr Cell kLevel8Gold[] = {{4, 7}, {7, 6}, {9, 3}, {5, 2}, {8, 1}, {13, 5}, {9, 7}};
constexpr WormholePair kLevel8Wormholes[] = {
    {{5, 7}, {6, 3}, 0, -10, 0, 10, 0},
    {{9, 5}, {8, 1}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel8Boosts[] = {{{3, 5}, 1, 0}};

constexpr const char* kLevel9Map[] = {
    "###############", "#.....#...#...#", "#.###.#.#.#.#.#",
    "#.#...X.....#.#", "#.###.#.#.#.#.#", "#...X...#.#.#.#",
    "###.#####.###.#", "#...#...#.....#", "#.###.#.#####.#",
    "#.#...#.......#", "###############",
};
constexpr Cell kLevel9Gold[] = {{3, 6}, {1, 2}, {5, 1}, {6, 5}, {10, 3}, {3, 9}, {5, 8}};
constexpr WormholePair kLevel9Wormholes[] = {
    {{2, 5}, {5, 4}, 0, -10, 10, 0, 0},
};
constexpr BoostPad kLevel9Boosts[] = {{{3, 5}, 1, 0}, {{5, 3}, 1, 0}};

constexpr const char* kLevel10Map[] = {
    "###############", "#.........X...#", "###.#####.#.#.#",
    "#...#.....#.#.#", "#.###.#######.#", "#.#...........#",
    "#.#.###.#####.#", "#.X...#...#.#.#", "#.###.###.#.#.#",
    "#.#...........#", "###############",
};
constexpr Cell kLevel10Gold[] = {
    {1, 3}, {5, 1}, {9, 3}, {5, 5}, {11, 5}, {11, 2}, {13, 9}, {11, 8},
};
constexpr WormholePair kLevel10Wormholes[] = {
    {{3, 3}, {5, 3}, 0, -10, 0, 10, 0},
    {{8, 1}, {11, 5}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel10Boosts[] = {{{9, 1}, 1, 0}, {{1, 7}, 1, 0}};

constexpr const char* kLevel11Map[] = {
    "#################", "#...X...#.......#", "#.#.#.#.###.###.#",
    "#.#.#.#.....#.#.#", "#.#.#.#######.#.#", "#.#.#...#...#...#",
    "#.#.#.#.###.#.###", "#.#...#...#.#.#.#", "#.#.#####.#.#...#",
    "#.#.......#.....#", "#################",
};
constexpr Cell kLevel11Gold[] = {
    {1, 3}, {3, 4}, {5, 6}, {6, 1}, {11, 3}, {9, 5}, {11, 6}, {15, 7},
};
constexpr WormholePair kLevel11Wormholes[] = {
    {{1, 1}, {5, 2}, 0, -10, 10, 0, 0},
};
constexpr BoostPad kLevel11Boosts[] = {{{3, 1}, 1, 0}};

constexpr const char* kLevel12Map[] = {
    "#################", "#.........#...#.#", "#####.#.#.#.#.#.#",
    "#...X.#.#...#...#", "#...###.#####.#.#", "#.#...#.#...#...#",
    "#.###.#.#.#.#.###", "#...#.#.#.#.#.#.#", "###.#...#.###.#.#",
    "#...X...#.......#", "#################",
};
constexpr Cell kLevel12Gold[] = {
    {1, 6}, {5, 5}, {7, 6}, {9, 1}, {12, 1}, {11, 7}, {10, 5}, {9, 8},
};
constexpr WormholePair kLevel12Wormholes[] = {
    {{1, 4}, {7, 2}, 0, -10, 10, 0, 0},
    {{6, 8}, {11, 1}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel12Boosts[] = {{{3, 9}, 1, 0}, {{3, 3}, 1, 0}};

constexpr const char* kLevel13Map[] = {
    "#################", "#.......#...#...#", "#.#.###.#.#.###.#",
    "#.#.#.....#.....#", "###.#########.#.#", "#...#.......#.#.#",
    "#.###.#####.X.#.#", "#.........#.#.#.#", "#..####.###.#.#.#",
    "#...#.......X...#", "#####.#########.#", "#.....#.........#",
    "#################",
};
constexpr Cell kLevel13Gold[] = {
    {5, 9}, {4, 7}, {2, 5}, {4, 1}, {8, 3}, {11, 3}, {7, 11}, {10, 11}, {13, 11},
};
constexpr WormholePair kLevel13Wormholes[] = {
    {{7, 7}, {5, 1}, 0, -10, 0, 10, 0},
    {{1, 5}, {11, 1}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel13Boosts[] = {{{11, 9}, 1, 0}, {{11, 6}, 1, 0}};

constexpr const char* kLevel14Map[] = {
    "#################", "#.....#.....X...#", "#####.#.#.#.#.#.#",
    "#...#...#.#.#.#.#", "#.#.#####.#.X.###", "#.#.....#.#.#...#",
    "#.#..##.#.#.###.#", "#.#...#...#...#.#", "#.#.#.#######.#.#",
    "#.#.#...#...#...#", "#.#.#.#.#.#.###.#", "#.#...#...#.....#",
    "#################",
};
constexpr Cell kLevel14Gold[] = {
    {1, 4}, {4, 6}, {7, 10}, {10, 9}, {15, 11}, {14, 5}, {15, 3}, {1, 1}, {13, 9},
};
constexpr WormholePair kLevel14Wormholes[] = {
    {{3, 4}, {11, 10}, -10, 0, 10, 0, 0},
    {{7, 9}, {15, 6}, 0, -10, 10, 0, 1},
};
constexpr BoostPad kLevel14Boosts[] = {{{11, 1}, 1, 0}, {{11, 4}, 1, 0}};

constexpr const char* kLevel15Map[] = {
    "###################", "#.....X...........#", "#.###.#####.#####.#",
    "#.#.......#.#.....#", "#.#######.#.#.###.#", "#...#.#...#.#...#.#",
    "#.#.#.#.#######.#.#", "#.#.#...........#.#", "#.#.X.###.###.#.###",
    "#.#.#...#.#...#...#", "###.##..#.#.#####.#", "#...X.....#.......#",
    "###################",
};
constexpr Cell kLevel15Gold[] = {
    {3, 6}, {1, 1}, {6, 3}, {7, 6}, {13, 7}, {13, 4}, {7, 1}, {11, 5}, {10, 1}, {13, 1},
};
constexpr WormholePair kLevel15Wormholes[] = {
    {{1, 4}, {7, 7}, -10, 0, 0, 10, 0},
    {{6, 3}, {14, 5}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel15Boosts[] = {
    {{5, 1}, 1, 0}, {{3, 11}, 1, 0}, {{3, 8}, 1, 0},
};

constexpr const char* kLevel16Map[] = {
    "###################", "#.....#.....#.....#", "#.###.#.#..##.#.#.#",
    "#...#...#.......#.#", "######X##.#####.#.#", "#.......X.......#.#",
    "#.#####.#.#.#####.#", "#...#.#.#.#.#.....#", "###.#.#.#.#.#######",
    "#...#.#.#.#.......#", "#.###...#########.#", "#.#...............#",
    "###################",
};
constexpr Cell kLevel16Gold[] = {
    {1, 7}, {6, 5}, {8, 11}, {16, 11}, {12, 9}, {13, 5}, {3, 3}, {1, 2}, {14, 7}, {17, 7},
};
constexpr WormholePair kLevel16Wormholes[] = {
    {{3, 5}, {17, 11}, 0, -10, 0, 10, 0},
    {{8, 11}, {11, 5}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel16Boosts[] = {{{7, 5}, 1, 0}, {{6, 5}, 0, -1}};

constexpr const char* kLevel17Map[] = {
    "###################", "#.......X.........#", "#.###.#.#.#######.#",
    "#.....#.#...#.#...#", "#####.#.#X#.#.#.###", "#.....#...#.#.....#",
    "#.#######.#.###.#.#", "#.#.......#...#...#", "#.#.#######.#.#.###",
    "#.#.#...........#.#", "#.X.#######.###.#.#", "#.#.........#.....#",
    "###################",
};
constexpr Cell kLevel17Gold[] = {
    {3, 5}, {7, 2}, {8, 7}, {3, 10}, {10, 11}, {15, 7}, {15, 1}, {12, 1}, {13, 3}, {9, 1},
};
constexpr WormholePair kLevel17Wormholes[] = {
    {{5, 2}, {4, 11}, -10, 0, 0, 10, 0},
    {{9, 7}, {15, 9}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel17Boosts[] = {
    {{7, 1}, 1, 0}, {{1, 10}, 1, 0}, {{9, 5}, 0, -1},
};

constexpr const char* kLevel18Map[] = {
    "###################", "#.......X.........#", "###.###.#########.#",
    "#...#...#.........#", "#.###.#X#.#######.#", "#.#...#...........#",
    "#.###.###########.#", "#...#.#.....#.....#", "###.#.#.#.#.#.###.#",
    "#...X.#.#...#.....#", "#.###.#.#.###.#.#.#", "#.#.....#.....#...#",
    "###################",
};
constexpr Cell kLevel18Gold[] = {
    {1, 7}, {3, 1}, {6, 3}, {5, 10}, {8, 7}, {11, 11}, {17, 9}, {9, 1}, {7, 5}, {12, 1}, {10, 3},
};
constexpr WormholePair kLevel18Wormholes[] = {
    {{3, 3}, {7, 8}, 0, -10, 10, 0, 0},
    {{5, 5}, {14, 9}, -10, 0, 0, 10, 1},
};
constexpr BoostPad kLevel18Boosts[] = {
    {{7, 1}, 1, 0}, {{7, 3}, 0, 1}, {{3, 9}, 1, 0},
};

constexpr const char* kLevel19Map[] = {
    "###################", "#.....X.......#...#", "#.#.#.#####.#...#.#",
    "#.#.#.....#.#...#.#", "###.#####.#.###.#.#", "#...#.....#.....#.#",
    "#.#####.#####.###.#", "#.#...#.#.........#", "#.#.#.X.#######.#.#",
    "#...#.#.#...#...#.#", "#####.#.#.#.#.#.#.#", "#.....X...#...#...#",
    "###################",
};
constexpr Cell kLevel19Gold[] = {
    {5, 7}, {1, 7}, {4, 1}, {9, 4}, {7, 10}, {11, 11}, {15, 7}, {7, 1}, {13, 1}, {10, 1}, {16, 1},
};
constexpr WormholePair kLevel19Wormholes[] = {
    {{1, 8}, {7, 9}, -10, 0, 10, 0, 0},
    {{5, 3}, {13, 9}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel19Boosts[] = {
    {{5, 11}, 1, 0}, {{5, 8}, 1, 0}, {{5, 1}, 1, 0},
};

constexpr const char* kLevel20Map[] = {
    "###################", "#.....X...........#", "###.#.###.#####.#.#",
    "#...#...X.#...#...#", "#.####..#...#.###.#", "#...#...#.#.#...#.#",
    "#.#.#.#X#.#.#.#.#.#", "#...#.#.....#...#.#", "###.#.#######.###.#",
    "#...#.#.....#...#.#", "#.###.#.###.###.#.#", "#.#.....#.......#.#",
    "###################",
};
constexpr Cell kLevel20Gold[] = {
    {1, 7}, {3, 1}, {6, 5}, {6, 11}, {11, 10}, {14, 9},
    {13, 3}, {9, 1}, {17, 11}, {17, 8}, {17, 5}, {17, 2},
};
constexpr WormholePair kLevel20Wormholes[] = {
    {{3, 2}, {13, 11}, -10, 0, 0, 10, 0},
    {{5, 10}, {11, 4}, -10, 0, 0, 10, 1},
};
constexpr BoostPad kLevel20Boosts[] = {
    {{5, 1}, 1, 0}, {{7, 3}, 1, 0}, {{7, 5}, 0, 1},
};

// Level 21 begins a recovery beat after level 20's long combined challenge.
constexpr const char* kLevel21Map[] = {
    "###############", "#.....#...#...#", "#.###.#.#.#.#.#",
    "#...#...#.#...#", "#.#.#####.#.###", "#...#.....#...#",
    "###.#.#######.#", "#...#.........#", "###############",
};
constexpr Cell kLevel21Gold[] = {{5, 3}, {9, 2}, {13, 6}, {7, 7}, {5, 5}, {2, 3}, {12, 1}};

constexpr const char* kLevel22Map[] = {
    "###############", "#...#.....#...#", "#.#.#.###.#.###",
    "#.#...#...#...#", "#.#####.#####.#", "#.#.....#.....#",
    "#.#.#####.#...#", "#.#.......#...#", "###############",
};
constexpr Cell kLevel22Gold[] = {{8, 3}, {9, 3}, {4, 5}, {4, 7}, {9, 2}, {11, 7}, {13, 7}};
constexpr WormholePair kLevel22Wormholes[] = {
    {{4, 3}, {7, 7}, -10, 0, 10, 0, 0},
};

constexpr const char* kLevel23Map[] = {
    "###############", "#.......#.....#", "##..##..#.#####",
    "#...#...#.....#", "#.###.#####...#", "#...#.#...#.#.#",
    "###.#.#.#.###.#", "#...#...#.....#", "###############",
};
constexpr Cell kLevel23Gold[] = {{5, 4}, {12, 7}, {13, 6}, {13, 4}, {12, 4}, {7, 2}, {2, 2}};
constexpr WormholePair kLevel23Wormholes[] = {
    {{3, 3}, {11, 7}, -10, 0, 10, 0, 0},
};

constexpr const char* kLevel24Map[] = {
    "#################", "#...#...........#", "#.#.#.#########.#",
    "#.#...#...#.....#", "#.#####.#.###.###", "#.#.....#...X...#",
    "#.#.##.####.##..#", "#.......#...#...#", "#########.###.###",
    "#.........#.....#", "#################",
};
constexpr Cell kLevel24Gold[] = {{8, 3}, {5, 9}, {7, 9}, {6, 5}, {4, 5}, {15, 2}, {11, 3}, {14, 9}};
constexpr WormholePair kLevel24Wormholes[] = {
    {{10, 7}, {1, 1}, -10, 0, 10, 0, 0},
};
constexpr BoostPad kLevel24Boosts[] = {{{13, 5}, -1, 0}};

constexpr const char* kLevel25Map[] = {
    "#################", "#.......X.......#", "#.##..#.###.#.#.#",
    "#.#...#...#.#.#.#", "###.#####.###.#.#", "#...#.#...#...#.#",
    "#.###...###.#####", "#...#.#...#.....#", "###.#.###.#####.#",
    "#...#...........#", "#################",
};
constexpr Cell kLevel25Gold[] = {{11, 9}, {13, 4}, {7, 1}, {2, 5}, {9, 7}, {15, 4}, {11, 2}, {7, 9}};
constexpr WormholePair kLevel25Wormholes[] = {
    {{3, 4}, {13, 9}, -10, 0, 10, 0, 0},
    {{7, 3}, {11, 6}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel25Boosts[] = {{{9, 1}, -1, 0}};

constexpr const char* kLevel26Map[] = {
    "#################", "#.........X.....#", "#...#####.#####.#",
    "#...#...#.#.....#", "#.#.#.#.#.#.###.#", "#...#.#.#.#.#...#",
    "#.###.#.#.#.#.###", "#.#...#...#.#.#.#", "#.#.#######.#.#.#",
    "#.#.........#...#", "#################",
};
constexpr Cell kLevel26Gold[] = {{13, 3}, {3, 9}, {14, 3}, {5, 5}, {9, 5}, {15, 9}, {1, 4}, {13, 1}};
constexpr WormholePair kLevel26Wormholes[] = {
    {{5, 1}, {4, 9}, -10, 0, 10, 0, 0},
    {{9, 7}, {11, 7}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel26Boosts[] = {{{11, 1}, -1, 0}};

constexpr const char* kLevel27Map[] = {
    "###################", "#.....X...........#", "###.#.X.#.#######.#",
    "#...#.#.#.....#.#.#", "#.###.#.#####.#.#.#", "#.#.#.#.#...#.#...#",
    "#.#.#.#.###.#.#.###", "#.#...#.....#.#...#", "#.#.#####...#.###.#",
    "#.#.#.......#...#.#", "#.#.###########.#.#", "#.#.............#.#",
    "###################",
};
constexpr Cell kLevel27Gold[] = {{2, 3}, {11, 11}, {12, 11}, {13, 4}, {14, 1}, {1, 10}, {11, 7}, {3, 6}, {10, 5}};
constexpr WormholePair kLevel27Wormholes[] = {
    {{4, 1}, {13, 9}, -10, 0, 10, 0, 0},
    {{3, 8}, {9, 3}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel27Boosts[] = {{{7, 1}, -1, 0}, {{5, 2}, 1, 0}};

constexpr const char* kLevel28Map[] = {
    "###################", "#.......#.........#", "#.#####.#####.###.#",
    "#.#...#.......#...#", "#.#.#.###.#.#######", "#.#.#.......#.....#",
    "#.#.##.##.###.###.#", "#.#.#...#.#...#...#", "#.###.#.###.###.###",
    "#.....#.......#...#", "####XX###########.#", "#.................#",
    "###################",
};
constexpr Cell kLevel28Gold[] = {{6, 6}, {8, 9}, {8, 5}, {15, 11}, {9, 5}, {15, 5}, {5, 8}, {15, 3}, {1, 6}};
constexpr WormholePair kLevel28Wormholes[] = {
    {{14, 11}, {7, 9}, -10, 0, 10, 0, 0},
    {{15, 7}, {11, 5}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel28Boosts[] = {{{5, 11}, 0, -1}, {{4, 9}, 0, 1}};

constexpr const char* kLevel29Map[] = {
    "###################", "#.....#.......X...#", "###.#.#.#####.#.###",
    "#...#...#...#.#...#", "#.#########.#.###.#", "#.#...#...#.#...#.#",
    "#.#.#...#.#.###.#.#", "#...#...#.#.#...#.#", "###.#####.#...###.#",
    "#.......#.#.#.#...#", "#.#####.#.#.#.#.#.#", "#.#.......X.....#.#",
    "###################",
};
constexpr Cell kLevel29Gold[] = {{15, 10}, {13, 7}, {4, 1}, {15, 9}, {15, 2}, {15, 11}, {8, 11}, {11, 3}, {9, 9}};
constexpr WormholePair kLevel29Wormholes[] = {
    {{2, 3}, {13, 9}, -10, 0, 10, 0, 0},
    {{7, 2}, {17, 7}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel29Boosts[] = {{{15, 1}, -1, 0}, {{11, 11}, -1, 0}};

constexpr const char* kLevel30Map[] = {
    "###################", "#.......#.....#...#", "#.#####.###.#.#.#.#",
    "#.....#...#.......#", "####..###.#.#####.#", "#.....#.#.#.....#.#",
    "#.#####.#.#.###.#.#", "#...#...#.#...#.#.#", "###.#.#.#.###.#.#.#",
    "#...X.#.#...#.#.#.#", "#.#X#.#####.###.#.#", "#.X.............#.#",
    "###################",
};
constexpr Cell kLevel30Gold[] = {{13, 5}, {15, 6}, {2, 1}, {9, 4}, {15, 11}, {17, 2}, {6, 11}, {13, 8}, {11, 2}, {5, 5}};
constexpr WormholePair kLevel30Wormholes[] = {
    {{4, 4}, {14, 11}, -10, 0, 10, 0, 0},
    {{5, 1}, {12, 5}, 0, -10, 0, 10, 1},
};
constexpr BoostPad kLevel30Boosts[] = {
    {{3, 11}, -1, 0}, {{5, 9}, -1, 0}, {{3, 9}, 0, 1},
};

// Level 31 starts the next 10-level chapter with another shorter recovery map.
constexpr const char* kLevel31Map[] = {
    "###############", "#.......#.....#", "#.#.###.#.#####",
    "#...#.#.#.#...#", "#.###.#.#.#...#", "#...#...#...#.#",
    "###.#.#######.#", "#...#.........#", "###############",
};
constexpr Cell kLevel31Gold[] = {{12, 1}, {12, 4}, {6, 7}, {13, 6}, {2, 5}, {13, 3}, {5, 4}};

constexpr const char* kLevel32Map[] = {
    "###############", "#...#...#...#.#", "#.#.#...#.#.#.#",
    "#.#...#.#.#.#.#", "#.#####.#.#.#.#", "#.#.....#.#.#.#",
    "#.#.#####.#...#", "#.#.......#...#", "###############",
};
constexpr Cell kLevel32Gold[] = {{11, 6}, {3, 1}, {12, 6}, {10, 1}, {9, 7}, {12, 7}, {5, 1}};
constexpr WormholePair kLevel32Wormholes[] = {
    {{4, 3}, {9, 3}, -10, 0, 10, 0, 0},
};

constexpr const char* kLevel33Map[] = {
    "###############", "#.........#...#", "#...#####.#.#.#",
    "#...#...#.#.#.#", "#.###...#.#.###", "#.#...#...#...#",
    "#.#.#########.#", "#.#...........#", "###############",
};
constexpr Cell kLevel33Gold[] = {{5, 4}, {1, 6}, {11, 7}, {5, 5}, {13, 6}, {5, 3}, {1, 1}};
constexpr WormholePair kLevel33Wormholes[] = {
    {{5, 1}, {7, 7}, -10, 0, 10, 0, 0},
};

constexpr const char* kLevel34Map[] = {
    "#################", "#.........#.....#", "#.###.###.###...#",
    "#.#...#.#...#...#", "#.#.###.###.#.###", "#.#...#.....#...#",
    "#.###.#####.###.#", "#...#.....#.#...#", "#########.#.#.#.#",
    "#.........X...#.#", "#################",
};
constexpr Cell kLevel34Gold[] = {{14, 3}, {9, 7}, {11, 6}, {15, 3}, {3, 9}, {1, 7}, {12, 1}, {1, 1}};
constexpr WormholePair kLevel34Wormholes[] = {
    {{8, 7}, {11, 7}, -10, 0, 10, 0, 0},
};
constexpr BoostPad kLevel34Boosts[] = {{{11, 9}, -1, 0}};

constexpr LevelDefinition kLevels[] = {
    {6, 6, kLevel1Map, {1, 4}, {4, 1}, kLevel1Gold, 2, nullptr, 0, nullptr, 0, 18},
    {10, 6, kLevel2Map, {1, 4}, {8, 1}, kLevel2Gold, 4, nullptr, 0, nullptr, 0, 28},
    {11, 7, kLevel3Map, {1, 5}, {9, 1}, kLevel3Gold, 5,
     kLevel3Wormholes, 1, nullptr, 0, 36},
    {13, 8, kLevel4Map, {1, 6}, {11, 1}, kLevel4Gold, 6,
     kLevel4Wormholes, 2, kLevel4Boosts, 1, 46},
    {13, 9, kLevel5Map, {1, 7}, {11, 1}, kLevel5Gold, 5,
     kLevel5Wormholes, 1, kLevel5Boosts, 1, 49},
    {13, 9, kLevel6Map, {1, 7}, {11, 1}, kLevel6Gold, 6,
     kLevel6Wormholes, 1, nullptr, 0, 59},
    {15, 9, kLevel7Map, {1, 7}, {13, 1}, kLevel7Gold, 6,
     kLevel7Wormholes, 1, kLevel7Boosts, 1, 64},
    {15, 9, kLevel8Map, {1, 7}, {13, 1}, kLevel8Gold, 7,
     kLevel8Wormholes, 2, kLevel8Boosts, 1, 74},
    {15, 11, kLevel9Map, {1, 9}, {13, 1}, kLevel9Gold, 7,
     kLevel9Wormholes, 1, kLevel9Boosts, 2, 84},
    {15, 11, kLevel10Map, {1, 9}, {13, 1}, kLevel10Gold, 8,
     kLevel10Wormholes, 2, kLevel10Boosts, 2, 94},
    {17, 11, kLevel11Map, {1, 9}, {15, 1}, kLevel11Gold, 8,
     kLevel11Wormholes, 1, kLevel11Boosts, 1, 99},
    {17, 11, kLevel12Map, {1, 9}, {15, 1}, kLevel12Gold, 8,
     kLevel12Wormholes, 2, kLevel12Boosts, 2, 109},
    {17, 13, kLevel13Map, {1, 11}, {15, 1}, kLevel13Gold, 9,
     kLevel13Wormholes, 2, kLevel13Boosts, 2, 114},
    {17, 13, kLevel14Map, {1, 11}, {15, 1}, kLevel14Gold, 9,
     kLevel14Wormholes, 2, kLevel14Boosts, 2, 124},
    {19, 13, kLevel15Map, {1, 11}, {17, 1}, kLevel15Gold, 10,
     kLevel15Wormholes, 2, kLevel15Boosts, 3, 129},
    {19, 13, kLevel16Map, {1, 11}, {17, 1}, kLevel16Gold, 10,
     kLevel16Wormholes, 2, kLevel16Boosts, 2, 139},
    {19, 13, kLevel17Map, {1, 11}, {17, 1}, kLevel17Gold, 10,
     kLevel17Wormholes, 2, kLevel17Boosts, 3, 149},
    {19, 13, kLevel18Map, {1, 11}, {17, 1}, kLevel18Gold, 11,
     kLevel18Wormholes, 2, kLevel18Boosts, 3, 159},
    {19, 13, kLevel19Map, {1, 11}, {17, 1}, kLevel19Gold, 11,
     kLevel19Wormholes, 2, kLevel19Boosts, 3, 169},
    {19, 13, kLevel20Map, {1, 11}, {17, 1}, kLevel20Gold, 12,
     kLevel20Wormholes, 2, kLevel20Boosts, 3, 179},
    {15, 9, kLevel21Map, {1, 7}, {13, 1}, kLevel21Gold, 7,
     nullptr, 0, nullptr, 0, 129},
    {15, 9, kLevel22Map, {1, 7}, {13, 1}, kLevel22Gold, 7,
     kLevel22Wormholes, 1, nullptr, 0, 129},
    {15, 9, kLevel23Map, {1, 7}, {13, 1}, kLevel23Gold, 7,
     kLevel23Wormholes, 1, nullptr, 0, 124},
    {17, 11, kLevel24Map, {1, 9}, {15, 1}, kLevel24Gold, 8,
     kLevel24Wormholes, 1, kLevel24Boosts, 1, 139},
    {17, 11, kLevel25Map, {1, 9}, {15, 1}, kLevel25Gold, 8,
     kLevel25Wormholes, 2, kLevel25Boosts, 1, 139},
    {17, 11, kLevel26Map, {1, 9}, {15, 1}, kLevel26Gold, 8,
     kLevel26Wormholes, 2, kLevel26Boosts, 1, 149},
    {19, 13, kLevel27Map, {1, 11}, {17, 1}, kLevel27Gold, 9,
     kLevel27Wormholes, 2, kLevel27Boosts, 2, 159},
    {19, 13, kLevel28Map, {1, 11}, {17, 1}, kLevel28Gold, 9,
     kLevel28Wormholes, 2, kLevel28Boosts, 2, 154},
    {19, 13, kLevel29Map, {1, 11}, {17, 1}, kLevel29Gold, 9,
     kLevel29Wormholes, 2, kLevel29Boosts, 2, 159},
    {19, 13, kLevel30Map, {1, 11}, {17, 1}, kLevel30Gold, 10,
     kLevel30Wormholes, 2, kLevel30Boosts, 3, 164},
    {15, 9, kLevel31Map, {1, 7}, {13, 1}, kLevel31Gold, 7,
     nullptr, 0, nullptr, 0, 124},
    {15, 9, kLevel32Map, {1, 7}, {13, 1}, kLevel32Gold, 7,
     kLevel32Wormholes, 1, nullptr, 0, 129},
    {15, 9, kLevel33Map, {1, 7}, {13, 1}, kLevel33Gold, 7,
     kLevel33Wormholes, 1, nullptr, 0, 124},
    {17, 11, kLevel34Map, {1, 9}, {15, 1}, kLevel34Gold, 8,
     kLevel34Wormholes, 1, kLevel34Boosts, 1, 139},
};
static_assert(sizeof(kLevels) / sizeof(kLevels[0]) == kImplementedLevels,
              "Level table and kImplementedLevels must stay in sync");

// Normal white-marble movement is 70% faster than the original tuning.
// Scale both acceleration and the cap so one does not cancel the other.
constexpr float kDefaultSpeedScale = 1.70f;
constexpr float kAcceleration = 155.0f * kDefaultSpeedScale;
constexpr float kDragPerSecond = 2.15f;
constexpr float kMaxSpeed = 92.0f * kDefaultSpeedScale;
constexpr float kBoostSpeed = 205.0f;
constexpr float kBoostSlowdown = 0.38f;
constexpr float kWormholeSlowdown = 0.82f;
constexpr uint32_t kBoostDurationMs = 900;
constexpr uint32_t kBoostCooldownMs = 1100;
constexpr uint32_t kBoostColorMs = 450;
constexpr uint32_t kWormholeCooldownMs = 750;
constexpr uint32_t kCountdownMs = 3000;
constexpr uint32_t kMaxPhysicsStepMs = 50;
constexpr float kNeutralRangeThresholdG = 0.08f;
constexpr size_t kNeutralSampleCount = 20;
constexpr int kViewportSize = 240;
constexpr int kPlanningPanStep = 72;

// Verified on the round Bubu board: the sensor axes match the display axes,
// but both signs are inverted relative to the direction the marble should roll.
// Keep these constants isolated so board orientation stays explicit.
constexpr bool kSwapImuAxes = false;
constexpr float kImuXSign = -1.0f;
constexpr float kImuYSign = -1.0f;

constexpr uint8_t kStarFinish = 1U << 0;
constexpr uint8_t kStarAllGold = 1U << 1;
constexpr uint8_t kStarSpeed = 1U << 2;

constexpr size_t kProgressHeaderBytes = 16;
constexpr size_t kProgressRecordBytes = 4;
constexpr size_t kProgressCrcBytes = 4;
constexpr size_t kProgressBytes =
    kProgressHeaderBytes + kMaxProgressLevels * kProgressRecordBytes + kProgressCrcBytes;
constexpr uint8_t kProgressVersion = 1;

struct ProgressRecord {
    uint8_t stars = 0;
    uint8_t best_gold = 0;
    uint16_t best_time_tenths = 0;
};

struct Progress {
    uint16_t current_level = 0;
    uint16_t highest_unlocked = 0;
    uint32_t revision = 0;
    std::array<ProgressRecord, kMaxProgressLevels> levels = {};
};

struct Runtime {
    State state;
    float latest_ax = 0.0f;
    float latest_ay = 0.0f;
    float latest_az = 0.0f;
    float neutral_ax = 0.0f;
    float neutral_ay = 0.0f;
    std::array<float, kNeutralSampleCount> sample_ax = {};
    std::array<float, kNeutralSampleCount> sample_ay = {};
    std::array<float, kNeutralSampleCount> sample_az = {};
    size_t sample_count = 0;
    size_t sample_cursor = 0;
    uint32_t last_tick_ms = 0;
    uint32_t countdown_end_ms = 0;
    uint32_t boost_until_ms = 0;
    uint32_t boost_color_until_ms = 0;
    uint32_t boost_cooldown_until_ms = 0;
    uint32_t wormhole_cooldown_until_ms = 0;
    int8_t boost_direction_x = 0;
    int8_t boost_direction_y = 0;
};

const char* TAG = "TiltMaze";
Runtime runtime;
Progress progress;
bool progress_loaded = false;

uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}

void WriteU16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFFU);
    p[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void WriteU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFFU);
    p[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    p[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    p[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint32_t ProgressChecksum(const uint8_t* data, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

void SaveProgress() {
    std::array<uint8_t, kProgressBytes> bytes = {};
    bytes[0] = 'B';
    bytes[1] = 'T';
    bytes[2] = 'M';
    bytes[3] = 'Z';
    bytes[4] = kProgressVersion;
    WriteU16(bytes.data() + 6, progress.current_level);
    WriteU16(bytes.data() + 8, progress.highest_unlocked);
    WriteU16(bytes.data() + 10, kMaxProgressLevels);
    WriteU32(bytes.data() + 12, progress.revision);
    for (size_t i = 0; i < progress.levels.size(); ++i) {
        const size_t offset = kProgressHeaderBytes + i * kProgressRecordBytes;
        bytes[offset] = progress.levels[i].stars;
        bytes[offset + 1] = progress.levels[i].best_gold;
        WriteU16(bytes.data() + offset + 2, progress.levels[i].best_time_tenths);
    }
    WriteU32(bytes.data() + kProgressBytes - kProgressCrcBytes,
             ProgressChecksum(bytes.data(), kProgressBytes - kProgressCrcBytes));

    Settings settings("tiltmaze", true);
    const esp_err_t result = settings.SetBlob("progress", bytes.data(), bytes.size());
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Progress write failed: %s", esp_err_to_name(result));
    }
}

void LoadProgress() {
    progress = {};
    Settings settings("tiltmaze", false);
    const std::vector<uint8_t> bytes = settings.GetBlob("progress");
    if (bytes.size() != kProgressBytes || bytes[0] != 'B' || bytes[1] != 'T' ||
        bytes[2] != 'M' || bytes[3] != 'Z' || bytes[4] != kProgressVersion ||
        ReadU16(bytes.data() + 10) != kMaxProgressLevels) {
        ESP_LOGI(TAG, "No compatible progress record; starting at level 1");
        return;
    }
    const uint32_t stored_crc = ReadU32(bytes.data() + kProgressBytes - kProgressCrcBytes);
    const uint32_t actual_crc =
        ProgressChecksum(bytes.data(), kProgressBytes - kProgressCrcBytes);
    if (stored_crc != actual_crc) {
        ESP_LOGW(TAG, "Progress checksum mismatch; ignoring record");
        return;
    }

    progress.current_level =
        std::min<uint16_t>(ReadU16(bytes.data() + 6), kImplementedLevels - 1);
    progress.highest_unlocked =
        std::min<uint16_t>(ReadU16(bytes.data() + 8), kImplementedLevels - 1);
    progress.revision = ReadU32(bytes.data() + 12);
    for (size_t i = 0; i < progress.levels.size(); ++i) {
        const size_t offset = kProgressHeaderBytes + i * kProgressRecordBytes;
        progress.levels[i].stars = bytes[offset] & 0x07U;
        progress.levels[i].best_gold = bytes[offset + 1];
        progress.levels[i].best_time_tenths = ReadU16(bytes.data() + offset + 2);
    }

    // Older builds left the player sitting on their completed final level
    // because there was no next level to unlock.  When content grows, advance
    // that one stale cap (or several historical caps) to the first unfinished
    // level without touching any stars or records.
    bool expanded_past_old_cap = false;
    while (progress.current_level == progress.highest_unlocked &&
           progress.highest_unlocked + 1 < kImplementedLevels &&
           (progress.levels[progress.highest_unlocked].stars & kStarFinish) != 0 &&
           (progress.levels[progress.highest_unlocked + 1].stars & kStarFinish) == 0) {
        ++progress.current_level;
        ++progress.highest_unlocked;
        expanded_past_old_cap = true;
    }
    if (expanded_past_old_cap) {
        ++progress.revision;
        SaveProgress();
        ESP_LOGI(TAG, "Unlocked new content at level %u", progress.current_level + 1);
    }
}

float CellCenter(uint8_t coordinate) {
    return static_cast<float>(coordinate * kCellSize + kCellSize / 2);
}

float ClampFloat(float value, float minimum, float maximum) {
    if (maximum < minimum) {
        return 0.0f;
    }
    return std::max(minimum, std::min(value, maximum));
}

void ClampCamera(float& camera_x, float& camera_y) {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    const float max_x = std::max(0.0f, level.width * kCellSize - kViewportSize * 1.0f);
    const float max_y = std::max(0.0f, level.height * kCellSize - kViewportSize * 1.0f);
    camera_x = ClampFloat(camera_x, 0.0f, max_x);
    camera_y = ClampFloat(camera_y, 0.0f, max_y);
}

void FollowBall() {
    runtime.state.camera_x = runtime.state.ball_x - kViewportSize * 0.5f;
    runtime.state.camera_y = runtime.state.ball_y - kViewportSize * 0.5f;
    ClampCamera(runtime.state.camera_x, runtime.state.camera_y);
}

int BreakableIndexAt(uint8_t col, uint8_t row) {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    int index = 0;
    for (uint8_t y = 0; y < level.height; ++y) {
        for (uint8_t x = 0; x < level.width; ++x) {
            if (level.rows[y][x] != 'X') {
                continue;
            }
            if (x == col && y == row) {
                return index;
            }
            ++index;
        }
    }
    return -1;
}

bool IsSolid(uint8_t col, uint8_t row) {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    if (col >= level.width || row >= level.height) {
        return true;
    }
    const char tile = level.rows[row][col];
    if (tile == '#') {
        return true;
    }
    if (tile != 'X') {
        return false;
    }
    const int index = BreakableIndexAt(col, row);
    return index < 0 || (runtime.state.broken_wall_mask & (1U << index)) == 0;
}

bool TryBreakWall(uint8_t col, uint8_t row) {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    if (!runtime.state.boost_active || col >= level.width || row >= level.height ||
        level.rows[row][col] != 'X') {
        return false;
    }
    const int index = BreakableIndexAt(col, row);
    if (index < 0 || index >= 16) {
        return false;
    }
    runtime.state.broken_wall_mask |= static_cast<uint16_t>(1U << index);
    runtime.state.boost_active = false;
    runtime.state.velocity_x *= kBoostSlowdown;
    runtime.state.velocity_y *= kBoostSlowdown;
    ESP_LOGI(TAG, "Breakable wall opened at %u,%u", col, row);
    return true;
}

void MoveAxis(float distance, bool horizontal) {
    State& state = runtime.state;
    float& position = horizontal ? state.ball_x : state.ball_y;
    float& velocity = horizontal ? state.velocity_x : state.velocity_y;
    position += distance;

    const LevelDefinition& level = kLevels[state.level_index];
    const int min_col = std::max(0, static_cast<int>((state.ball_x - kBallRadius) / kCellSize));
    const int max_col = std::min<int>(level.width - 1,
        static_cast<int>((state.ball_x + kBallRadius) / kCellSize));
    const int min_row = std::max(0, static_cast<int>((state.ball_y - kBallRadius) / kCellSize));
    const int max_row = std::min<int>(level.height - 1,
        static_cast<int>((state.ball_y + kBallRadius) / kCellSize));

    for (int row = min_row; row <= max_row; ++row) {
        for (int col = min_col; col <= max_col; ++col) {
            if (!IsSolid(static_cast<uint8_t>(col), static_cast<uint8_t>(row))) {
                continue;
            }
            const float left = col * kCellSize * 1.0f;
            const float top = row * kCellSize * 1.0f;
            const float right = left + kCellSize;
            const float bottom = top + kCellSize;
            const float nearest_x = ClampFloat(state.ball_x, left, right);
            const float nearest_y = ClampFloat(state.ball_y, top, bottom);
            const float dx = state.ball_x - nearest_x;
            const float dy = state.ball_y - nearest_y;
            if (dx * dx + dy * dy >= kBallRadius * kBallRadius) {
                continue;
            }
            if (TryBreakWall(static_cast<uint8_t>(col), static_cast<uint8_t>(row))) {
                continue;
            }
            if (horizontal) {
                state.ball_x = distance > 0.0f ? left - kBallRadius : right + kBallRadius;
            } else {
                state.ball_y = distance > 0.0f ? top - kBallRadius : bottom + kBallRadius;
            }
            velocity = 0.0f;
        }
    }
}

float DistanceSquared(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

void CollectGold() {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    for (uint8_t i = 0; i < level.gold_count && i < 16; ++i) {
        if ((runtime.state.gold_collected_mask & (1U << i)) != 0) {
            continue;
        }
        if (DistanceSquared(runtime.state.ball_x, runtime.state.ball_y,
                            CellCenter(level.gold[i].col), CellCenter(level.gold[i].row)) <=
            13.0f * 13.0f) {
            runtime.state.gold_collected_mask |= static_cast<uint16_t>(1U << i);
        }
    }
}

void HandleWormholes(uint32_t now_ms) {
    if (now_ms < runtime.wormhole_cooldown_until_ms) {
        return;
    }
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    for (uint8_t i = 0; i < level.wormhole_count; ++i) {
        const WormholePair& pair = level.wormholes[i];
        const float ax = CellCenter(pair.a.col) + pair.a_offset_x;
        const float ay = CellCenter(pair.a.row) + pair.a_offset_y;
        const float bx = CellCenter(pair.b.col) + pair.b_offset_x;
        const float by = CellCenter(pair.b.row) + pair.b_offset_y;
        bool at_a = DistanceSquared(runtime.state.ball_x, runtime.state.ball_y, ax, ay) <= 7.0f * 7.0f;
        bool at_b = DistanceSquared(runtime.state.ball_x, runtime.state.ball_y, bx, by) <= 7.0f * 7.0f;
        if (!at_a && !at_b) {
            continue;
        }
        runtime.state.ball_x = at_a ? bx : ax;
        runtime.state.ball_y = at_a ? by : ay;
        runtime.state.velocity_x *= kWormholeSlowdown;
        runtime.state.velocity_y *= kWormholeSlowdown;
        runtime.wormhole_cooldown_until_ms = now_ms + kWormholeCooldownMs;
        return;
    }
}

void HandleBoosts(uint32_t now_ms) {
    if (runtime.state.boost_active && now_ms >= runtime.boost_until_ms) {
        runtime.state.boost_active = false;
    }
    runtime.state.boost_style = now_ms < runtime.boost_color_until_ms ? 1 : 0;
    if (now_ms < runtime.boost_cooldown_until_ms || runtime.state.boost_active) {
        return;
    }
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    for (uint8_t i = 0; i < level.boost_count; ++i) {
        const BoostPad& boost = level.boosts[i];
        const float x = CellCenter(boost.cell.col);
        const float y = CellCenter(boost.cell.row);
        if (DistanceSquared(runtime.state.ball_x, runtime.state.ball_y, x, y) > 13.0f * 13.0f) {
            continue;
        }
        // Snap to the shot axis and replace velocity: one touch is sufficient;
        // the child does not need to keep tilting to break the aligned wall.
        if (boost.direction_x != 0) {
            runtime.state.ball_y = y;
        } else {
            runtime.state.ball_x = x;
        }
        runtime.boost_direction_x = boost.direction_x;
        runtime.boost_direction_y = boost.direction_y;
        runtime.state.velocity_x = boost.direction_x * kBoostSpeed;
        runtime.state.velocity_y = boost.direction_y * kBoostSpeed;
        runtime.state.boost_active = true;
        runtime.state.boost_style = 1;
        runtime.boost_until_ms = now_ms + kBoostDurationMs;
        runtime.boost_color_until_ms = now_ms + kBoostColorMs;
        runtime.boost_cooldown_until_ms = now_ms + kBoostCooldownMs;
        return;
    }
}

void CompleteLevel() {
    State& state = runtime.state;
    state.phase = Phase::kComplete;
    state.velocity_x = 0.0f;
    state.velocity_y = 0.0f;
    state.boost_active = false;

    const LevelDefinition& level = kLevels[state.level_index];
    uint8_t stars = kStarFinish;
    const uint16_t all_gold_mask =
        level.gold_count >= 16 ? 0xFFFFU : static_cast<uint16_t>((1U << level.gold_count) - 1U);
    if ((state.gold_collected_mask & all_gold_mask) == all_gold_mask) {
        stars |= kStarAllGold;
    }
    if (state.elapsed_ms <= static_cast<uint32_t>(level.target_seconds) * 1000U) {
        stars |= kStarSpeed;
    }
    state.last_run_stars = stars;

    ProgressRecord& record = progress.levels[state.level_index];
    record.stars |= stars;
    record.best_gold = std::max<uint8_t>(record.best_gold, CountCollectedGold());
    const uint16_t tenths = static_cast<uint16_t>(
        std::min<uint32_t>((state.elapsed_ms + 50U) / 100U, 0xFFFFU));
    if (record.best_time_tenths == 0 || tenths < record.best_time_tenths) {
        record.best_time_tenths = tenths;
    }
    if (state.level_index + 1 < kImplementedLevels) {
        progress.current_level = state.level_index + 1;
        progress.highest_unlocked = std::max<uint16_t>(
            progress.highest_unlocked, state.level_index + 1);
    } else {
        progress.current_level = state.level_index;
    }
    ++progress.revision;
    SaveProgress();
}

void CheckGoal() {
    const LevelDefinition& level = kLevels[runtime.state.level_index];
    if (DistanceSquared(runtime.state.ball_x, runtime.state.ball_y,
                        CellCenter(level.goal.col), CellCenter(level.goal.row)) <=
        14.0f * 14.0f) {
        CompleteLevel();
    }
}

}  // namespace

void Initialize() {
    if (progress_loaded) {
        return;
    }
    LoadProgress();
    progress_loaded = true;
}

const LevelDefinition& GetLevel(uint8_t index) {
    return kLevels[std::min<uint8_t>(index, kImplementedLevels - 1)];
}

const State& GetState() {
    return runtime.state;
}

uint8_t GetCurrentLevel() {
    Initialize();
    return static_cast<uint8_t>(std::min<uint16_t>(progress.current_level,
                                                   kImplementedLevels - 1));
}

uint8_t GetHighestUnlockedLevel() {
    Initialize();
    return static_cast<uint8_t>(std::min<uint16_t>(progress.highest_unlocked,
                                                   kImplementedLevels - 1));
}

uint8_t GetSavedStars(uint16_t level_index) {
    Initialize();
    return level_index < progress.levels.size() ? progress.levels[level_index].stars : 0;
}

uint32_t GetBestTimeMs(uint16_t level_index) {
    Initialize();
    return level_index < progress.levels.size()
        ? progress.levels[level_index].best_time_tenths * 100U : 0U;
}

void StartLevel(uint8_t index, uint32_t now_ms) {
    Initialize();
    index = std::min<uint8_t>(index, GetHighestUnlockedLevel());
    index = std::min<uint8_t>(index, kImplementedLevels - 1);
    runtime = {};
    runtime.state.phase = Phase::kCalibrating;
    runtime.state.level_index = index;
    const LevelDefinition& level = kLevels[index];
    runtime.state.ball_x = CellCenter(level.start.col);
    runtime.state.ball_y = CellCenter(level.start.row);
    runtime.last_tick_ms = now_ms;
    FollowBall();
}

void Stop() {
    runtime.state.phase = Phase::kStopped;
    runtime.state.velocity_x = 0.0f;
    runtime.state.velocity_y = 0.0f;
    runtime.state.boost_active = false;
}

void FeedAccel(float ax, float ay, float az, uint32_t) {
    runtime.latest_ax = ax;
    runtime.latest_ay = ay;
    runtime.latest_az = az;
    runtime.sample_ax[runtime.sample_cursor] = ax;
    runtime.sample_ay[runtime.sample_cursor] = ay;
    runtime.sample_az[runtime.sample_cursor] = az;
    runtime.sample_cursor = (runtime.sample_cursor + 1) % kNeutralSampleCount;
    runtime.sample_count = std::min(runtime.sample_count + 1, kNeutralSampleCount);

    if (runtime.sample_count < kNeutralSampleCount) {
        runtime.state.neutral_stable = false;
        return;
    }
    auto range = [](const auto& samples) {
        const auto result = std::minmax_element(samples.begin(), samples.end());
        return *result.second - *result.first;
    };
    runtime.state.neutral_stable = range(runtime.sample_ax) <= kNeutralRangeThresholdG &&
                                   range(runtime.sample_ay) <= kNeutralRangeThresholdG &&
                                   range(runtime.sample_az) <= kNeutralRangeThresholdG;
}

bool ConfirmNeutral(uint32_t now_ms) {
    if (runtime.state.phase != Phase::kCalibrating || !runtime.state.neutral_stable ||
        runtime.sample_count == 0) {
        return false;
    }
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    for (size_t i = 0; i < runtime.sample_count; ++i) {
        sum_x += runtime.sample_ax[i];
        sum_y += runtime.sample_ay[i];
    }
    runtime.neutral_ax = sum_x / runtime.sample_count;
    runtime.neutral_ay = sum_y / runtime.sample_count;
    runtime.state.phase = Phase::kCountdown;
    runtime.countdown_end_ms = now_ms + kCountdownMs;
    runtime.state.countdown_remaining_ms = kCountdownMs;
    runtime.last_tick_ms = now_ms;
    return true;
}

void Tick(uint32_t now_ms) {
    State& state = runtime.state;
    if (state.phase == Phase::kStopped || state.phase == Phase::kCalibrating ||
        state.phase == Phase::kPlanning || state.phase == Phase::kComplete) {
        runtime.last_tick_ms = now_ms;
        return;
    }
    if (state.phase == Phase::kCountdown) {
        if (static_cast<int32_t>(now_ms - runtime.countdown_end_ms) >= 0) {
            state.phase = Phase::kPlaying;
            state.countdown_remaining_ms = 0;
            runtime.last_tick_ms = now_ms;
        } else {
            state.countdown_remaining_ms = runtime.countdown_end_ms - now_ms;
        }
        return;
    }

    const uint32_t dt_ms = std::min<uint32_t>(now_ms - runtime.last_tick_ms, kMaxPhysicsStepMs);
    runtime.last_tick_ms = now_ms;
    if (dt_ms == 0) {
        return;
    }
    state.elapsed_ms += dt_ms;
    const float dt = dt_ms / 1000.0f;

    if (state.boost_active) {
        state.velocity_x = runtime.boost_direction_x * kBoostSpeed;
        state.velocity_y = runtime.boost_direction_y * kBoostSpeed;
    } else {
        float raw_x = runtime.latest_ax - runtime.neutral_ax;
        float raw_y = runtime.latest_ay - runtime.neutral_ay;
        const float tilt_x = (kSwapImuAxes ? raw_y : raw_x) * kImuXSign;
        const float tilt_y = (kSwapImuAxes ? raw_x : raw_y) * kImuYSign;
        state.velocity_x += tilt_x * kAcceleration * dt;
        state.velocity_y += tilt_y * kAcceleration * dt;
        const float drag = std::exp(-kDragPerSecond * dt);
        state.velocity_x *= drag;
        state.velocity_y *= drag;
        const float speed = std::sqrt(state.velocity_x * state.velocity_x +
                                      state.velocity_y * state.velocity_y);
        if (speed > kMaxSpeed) {
            state.velocity_x *= kMaxSpeed / speed;
            state.velocity_y *= kMaxSpeed / speed;
        }
    }

    MoveAxis(state.velocity_x * dt, true);
    MoveAxis(state.velocity_y * dt, false);
    HandleBoosts(now_ms);
    HandleWormholes(now_ms);
    CollectGold();
    CheckGoal();
    FollowBall();
}

bool EnterPlanning() {
    if (runtime.state.phase != Phase::kPlaying) {
        return false;
    }
    runtime.state.phase = Phase::kPlanning;
    return true;
}

void PanPlanning(PanDirection direction) {
    if (runtime.state.phase != Phase::kPlanning) {
        return;
    }
    switch (direction) {
        case PanDirection::kUp:    runtime.state.camera_y -= kPlanningPanStep; break;
        case PanDirection::kDown:  runtime.state.camera_y += kPlanningPanStep; break;
        case PanDirection::kLeft:  runtime.state.camera_x -= kPlanningPanStep; break;
        case PanDirection::kRight: runtime.state.camera_x += kPlanningPanStep; break;
    }
    ClampCamera(runtime.state.camera_x, runtime.state.camera_y);
}

bool ResumeFromPlanning(uint32_t now_ms) {
    if (runtime.state.phase != Phase::kPlanning) {
        return false;
    }
    runtime.state.phase = Phase::kCountdown;
    runtime.countdown_end_ms = now_ms + kCountdownMs;
    runtime.state.countdown_remaining_ms = kCountdownMs;
    runtime.last_tick_ms = now_ms;
    FollowBall();
    return true;
}

bool IsGoldCollected(uint8_t index) {
    return index < 16 && (runtime.state.gold_collected_mask & (1U << index)) != 0;
}

bool IsBreakableBroken(uint8_t col, uint8_t row) {
    const int index = BreakableIndexAt(col, row);
    return index >= 0 && index < 16 &&
           (runtime.state.broken_wall_mask & (1U << index)) != 0;
}

uint8_t CountCollectedGold() {
    uint16_t mask = runtime.state.gold_collected_mask;
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

}  // namespace TiltMazeGame
