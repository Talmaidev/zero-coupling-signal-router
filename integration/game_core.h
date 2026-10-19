/**
 * game_core.h - 2048 游戏核心层
 * 纯游戏逻辑：位板表示、预计算移动表、移动、计分、随机块
 * 不依赖任何 AI 算法，可独立编译使用
 */
#ifndef GAME_CORE_H
#define GAME_CORE_H

#include <stdint.h>

typedef uint64_t board_t;

/* 初始化预计算表（必须在使用前调用一次） */
void game_core_init(void);

/* 创建新棋盘（含 2 个随机块） */
board_t game_new_board(void);

/* 执行移动，返回得分；移动无效返回 -1
 * move: 0=上, 1=下, 2=左, 3=右
 * 注意：此函数不添加随机块，调用方需自行调用 game_add_random_tile */
int game_execute_move(board_t *b, int move);

/* 在随机空格添加新块（90% 为 2，10% 为 4） */
board_t game_add_random_tile(board_t b);

/* 判断游戏是否结束 */
int game_is_game_over(board_t b);

/* 统计空格数 */
int game_count_empty(board_t b);

/* 计算棋盘总得分 */
int game_get_total_score(board_t b);

/* 获取棋盘实际值（0,2,4,8...），输出 16 元素数组（行优先） */
void game_get_board_values(board_t b, int *out);

/* 获取棋盘 log2 值（0=空,1=2,2=4...），输出 16 元素数组（行优先） */
void game_get_board_log2(board_t b, int *out);

/* 单方向移动（返回新棋盘，不计分） */
board_t game_move_up(board_t b);
board_t game_move_down(board_t b);
board_t game_move_left(board_t b);
board_t game_move_right(board_t b);

#endif /* GAME_CORE_H */
