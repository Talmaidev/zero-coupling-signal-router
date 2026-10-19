/**
 * game_core.c - 2048 游戏核心实现
 * 位板布局：行优先，row 0 在 bits 0-15；每行内 col 0（最左）在 bits 12-15
 * 预计算 65536 行的左移/右移结果和分数表
 */
#include "game_core.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint16_t row_left_table[65536];
static uint16_t row_right_table[65536];
static int      row_score_table[65536];
static int      tables_inited = 0;

static inline uint16_t get_row(board_t b, int r)
{
    return (uint16_t)(b >> (r * 16));
}

static inline board_t set_row(int r, uint16_t row)
{
    return (board_t)row << (r * 16);
}

/* 计算单行左移结果：col 0 在 bits 12-15（最左） */
static uint16_t calc_row_left(uint16_t row)
{
    int t[4] = {
        (row >> 12) & 0xF,
        (row >> 8) & 0xF,
        (row >> 4) & 0xF,
        row & 0xF
    };
    int i = 0, w = 0;
    while (i < 4)
    {
        while (i < 4 && t[i] == 0) i++;
        if (i >= 4) break;
        int val = t[i++];
        while (i < 4 && t[i] == 0) i++;
        if (i < 4 && t[i] == val)
        {
            val++;
            i++;
        }
        t[w++] = val;
    }
    while (w < 4) t[w++] = 0;
    return (uint16_t)((t[0] << 12) | (t[1] << 8) | (t[2] << 4) | t[3]);
}

void game_core_init(void)
{
    if (tables_inited) return;
    for (int r = 0; r < 65536; r++)
    {
        row_left_table[r] = calc_row_left((uint16_t)r);
        /* 右移 = 反转行 -> 左移 -> 反转 */
        uint16_t rev = (uint16_t)(
            ((r & 0x000F) << 12) | ((r & 0x00F0) << 4) |
            ((r & 0x0F00) >> 4)  | ((r & 0xF000) >> 12)
        );
        uint16_t rl = calc_row_left(rev);
        row_right_table[r] = (uint16_t)(
            ((rl & 0x000F) << 12) | ((rl & 0x00F0) << 4) |
            ((rl & 0x0F00) >> 4)  | ((rl & 0xF000) >> 12)
        );
        /* 原始行的分数（不是左移后的行） */
        int s = 0;
        for (int i = 0; i < 4; i++)
        {
            int v = (r >> (i * 4)) & 0xF;
            if (v >= 2) s += (v - 1) * (1 << v);
        }
        row_score_table[r] = s;
    }
    srand((unsigned)time(NULL));
    tables_inited = 1;
}

board_t game_move_left(board_t b)
{
    return set_row(0, row_left_table[get_row(b, 0)])
         | set_row(1, row_left_table[get_row(b, 1)])
         | set_row(2, row_left_table[get_row(b, 2)])
         | set_row(3, row_left_table[get_row(b, 3)]);
}

board_t game_move_right(board_t b)
{
    return set_row(0, row_right_table[get_row(b, 0)])
         | set_row(1, row_right_table[get_row(b, 1)])
         | set_row(2, row_right_table[get_row(b, 2)])
         | set_row(3, row_right_table[get_row(b, 3)]);
}

/* 提取列 c：每行内 col c 的位偏移是 (3-c)*4；打包后 row0 在 bits 12-15 */
static inline uint16_t get_col(board_t b, int c)
{
    int off = (3 - c) * 4;
    return (uint16_t)(
        ((b >> off) & 0xF) << 12 |
        ((b >> (16 + off)) & 0xF) << 8 |
        ((b >> (32 + off)) & 0xF) << 4 |
        ((b >> (48 + off)) & 0xF)
    );
}

/* 将列值写回列 c：row0 从 bits 12-15 取出 */
static inline board_t set_col(int c, uint16_t col)
{
    int off = (3 - c) * 4;
    return (board_t)((col >> 12) & 0xF) << off
         | (board_t)((col >> 8) & 0xF) << (16 + off)
         | (board_t)((col >> 4) & 0xF) << (32 + off)
         | (board_t)(col & 0xF) << (48 + off);
}

board_t game_move_up(board_t b)
{
    return set_col(0, row_left_table[get_col(b, 0)])
         | set_col(1, row_left_table[get_col(b, 1)])
         | set_col(2, row_left_table[get_col(b, 2)])
         | set_col(3, row_left_table[get_col(b, 3)]);
}

board_t game_move_down(board_t b)
{
    return set_col(0, row_right_table[get_col(b, 0)])
         | set_col(1, row_right_table[get_col(b, 1)])
         | set_col(2, row_right_table[get_col(b, 2)])
         | set_col(3, row_right_table[get_col(b, 3)]);
}

int game_count_empty(board_t b)
{
    board_t x = b;
    x = x | (x >> 2);
    x = x | (x >> 1);
    x = x & 0x1111111111111111ULL;
    x = x + (x >> 32);
    x = x + (x >> 16);
    x = x + (x >> 8);
    x = x + (x >> 4);
    return 16 - (int)(x & 0xF);
}

board_t game_add_random_tile(board_t b)
{
    int empty = game_count_empty(b);
    if (empty == 0) return b;
    int idx = rand() % empty;
    int val = (rand() % 10 < 9) ? 1 : 2;
    board_t tmp = b;
    for (int i = 0; i < 16; i++)
    {
        if ((tmp & 0xF) == 0)
        {
            if (idx == 0)
            {
                b |= (board_t)val << (i * 4);
                return b;
            }
            idx--;
        }
        tmp >>= 4;
    }
    return b;
}

int game_execute_move(board_t *b, int move)
{
    board_t nb;
    switch (move)
    {
        case 0: nb = game_move_up(*b);    break;
        case 1: nb = game_move_down(*b);  break;
        case 2: nb = game_move_left(*b);  break;
        case 3: nb = game_move_right(*b); break;
        default: return -1;
    }
    if (nb == *b) return -1;
    int score = 0;
    for (int i = 0; i < 4; i++)
    {
        score += row_score_table[get_row(nb, i)]
               - row_score_table[get_row(*b, i)];
    }
    *b = nb;
    return score;
}

int game_is_game_over(board_t b)
{
    if (game_count_empty(b) > 0) return 0;
    if (game_move_left(b) != b || game_move_right(b) != b) return 0;
    if (game_move_up(b) != b || game_move_down(b) != b) return 0;
    return 1;
}

board_t game_new_board(void)
{
    board_t b = 0;
    b = game_add_random_tile(b);
    b = game_add_random_tile(b);
    return b;
}

int game_get_total_score(board_t b)
{
    int s = 0;
    for (int i = 0; i < 4; i++) s += row_score_table[get_row(b, i)];
    return s;
}

void game_get_board_values(board_t b, int *out)
{
    for (int i = 0; i < 16; i++)
    {
        int v = (int)((b >> (i * 4)) & 0xF);
        out[i] = v == 0 ? 0 : (1 << v);
    }
}

void game_get_board_log2(board_t b, int *out)
{
    for (int i = 0; i < 16; i++)
    {
        out[i] = (int)((b >> (i * 4)) & 0xF);
    }
}
