#ifndef SIM_H
#define SIM_H

/* Approximate street-fight slice. Numbers are not from the encrypted module.
   See port/RECONSTRUCAO.md for what was observed on the iOS reference video
   and what was filled in here. */

#include <stdint.h>

enum {
    SIM_LEFT = 1 << 0,
    SIM_RIGHT = 1 << 1,
    SIM_UP = 1 << 2,
    SIM_DOWN = 1 << 3,
    SIM_PUNCH = 1 << 4,
    SIM_KICK = 1 << 5,
    SIM_SPECIAL = 1 << 6,
    SIM_JUMP = 1 << 7,
    SIM_RESTART = 1 << 8
};

enum {
    ST_IDLE = 0,
    ST_WALK,
    ST_PUNCH,
    ST_KICK,
    ST_SPECIAL,
    ST_HURT,
    ST_DOWN,
    ST_DEAD,
    ST_AIR
};

enum { SIM_ENEMIES = 4 };

typedef struct Actor {
    int x;
    int depth;
    int alt;
    int vy;
    int facing; /* +1 screen right, -1 screen left */
    int hp;
    int state;
    int timer;
    int cooldown;
    int flash;
    int hurt_lock;
    int attack_id;
    int last_hit_by;
    int alive;
} Actor;

typedef struct Sim {
    Actor player;
    Actor enemy[SIM_ENEMIES];
    int enemy_n;
    int wave; /* 1 then 2 */
    int wave2_armed;
    int camera_x;
    int camera_locked;
    int go_timer;
    int score;
    int chain;
    int chain_age;
    int time_left;
    int result; /* 0 playing, 1 clear, 2 down */
    int tick;
    unsigned prev_buttons;
    int next_attack;
} Sim;

void sim_init(Sim *s);
void sim_tick(Sim *s, unsigned buttons);

/* Test placement. Not a gameplay cheat and not a recovered debug mode. */
void sim_place_player(Sim *s, int x, int depth);
void sim_place_enemy(Sim *s, int index, int x, int depth, int hp);

int sim_living_enemies(const Sim *s);

#endif
