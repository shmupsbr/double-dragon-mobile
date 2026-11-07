#include "sim.h"

#include <stdio.h>

static int g_fail;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", what);
        g_fail++;
    }
}

static void test_move_and_depth(void) {
    Sim s;
    int x0, d0, i;
    sim_init(&s);
    x0 = s.player.x;
    for (i = 0; i < 12; i++) {
        sim_tick(&s, SIM_RIGHT);
    }
    check(s.player.x > x0, "right increases x");
    check(s.player.facing == 1, "facing right");
    for (i = 0; i < 8; i++) {
        sim_tick(&s, SIM_LEFT);
    }
    check(s.player.facing == -1, "facing left");
    d0 = s.player.depth;
    for (i = 0; i < 6; i++) {
        sim_tick(&s, SIM_UP);
    }
    check(s.player.depth < d0, "up moves to the back of the street");
    d0 = s.player.depth;
    for (i = 0; i < 6; i++) {
        sim_tick(&s, SIM_DOWN);
    }
    check(s.player.depth > d0, "down moves toward the camera");
    sim_init(&s);
    for (i = 0; i < 400; i++) {
        sim_tick(&s, SIM_RIGHT);
    }
    check(s.camera_locked == 1, "camera stays locked during the first wave");
    check(s.player.x <= 210, "player cannot walk past the locked camera");
    check(s.camera_x == 0, "locked camera origin");
}

static void test_depth_gate_and_hit(void) {
    Sim s;
    int i;
    sim_init(&s);
    sim_place_player(&s, 100, 100);
    sim_place_enemy(&s, 0, 124, 100, 36);
    sim_place_enemy(&s, 1, 124, 180, 36);
    sim_place_enemy(&s, 2, 0, 0, 0);
    s.player.facing = 1;
    int reacted = 0;
    sim_tick(&s, SIM_PUNCH);
    for (i = 0; i < 24; i++) {
        sim_tick(&s, 0);
        if (s.enemy[0].flash > 0 || s.enemy[0].state == ST_HURT || s.enemy[0].state == ST_DOWN) {
            reacted = 1;
        }
    }
    check(s.enemy[0].hp < 36, "punch lands on the aligned enemy");
    check(s.enemy[1].hp == 36, "same x but far depth is not a hit");
    check(reacted, "aligned enemy shows a hit reaction");
    check(s.score > 0, "a landed hit scores");
}

static void test_enemy_aligns_before_strike(void) {
    Sim s;
    int i;
    int hp;
    sim_init(&s);
    sim_place_player(&s, 100, 120);
    sim_place_enemy(&s, 0, 122, 40, 36);
    sim_place_enemy(&s, 1, 0, 0, 0);
    sim_place_enemy(&s, 2, 0, 0, 0);
    hp = s.player.hp;
    for (i = 0; i < 20; i++) {
        sim_tick(&s, 0);
    }
    check(s.enemy[0].depth > 40, "enemy walks up the street toward the player");
    check(s.player.hp == hp, "enemy does not strike while depth is still off");
}

static void test_jump(void) {
    Sim s;
    int i;
    int saw = 0;
    sim_init(&s);
    sim_place_enemy(&s, 0, 0, 0, 0);
    sim_place_enemy(&s, 1, 0, 0, 0);
    sim_place_enemy(&s, 2, 0, 0, 0);
    sim_tick(&s, SIM_JUMP);
    for (i = 0; i < 20; i++) {
        if (s.player.alt > 0) {
            saw = 1;
        }
        sim_tick(&s, 0);
    }
    check(saw, "jump leaves the ground");
    check(s.player.alt == 0, "jump returns to the ground");
}

static void test_clear_and_restart(void) {
    Sim s;
    int i;
    sim_init(&s);
    sim_place_player(&s, 100, 100);
    sim_place_enemy(&s, 0, 124, 100, 8);
    sim_place_enemy(&s, 1, 124, 100, 8);
    sim_place_enemy(&s, 2, 124, 100, 8);
    s.player.facing = 1;
    sim_tick(&s, SIM_PUNCH);
    for (i = 0; i < 30; i++) {
        sim_tick(&s, 0);
    }
    check(sim_living_enemies(&s) == 0, "one punch clears a hurt wave placed in range");
    check(s.camera_locked == 0, "clearing the wave unlocks the camera");
    check(s.go_timer > 0, "go signal after the wave");
    for (i = 0; i < 200; i++) {
        sim_tick(&s, SIM_RIGHT);
    }
    check(s.wave == 2, "walking right after go starts the next wave");
    sim_place_player(&s, s.player.x, 100);
    sim_place_enemy(&s, 0, s.player.x + 24, 100, 8);
    sim_place_enemy(&s, 1, s.player.x + 24, 100, 8);
    sim_place_enemy(&s, 2, s.player.x + 24, 100, 8);
    s.player.facing = 1;
    s.player.state = ST_IDLE;
    sim_tick(&s, SIM_PUNCH);
    for (i = 0; i < 40; i++) {
        sim_tick(&s, 0);
    }
    check(s.result == 1, "clearing the second wave wins");
    sim_tick(&s, SIM_RESTART);
    check(s.result == 0, "restart clears the end card");
    check(s.wave == 1, "restart returns to the first wave");
    check(s.player.hp == 48, "restart restores health");
}

static void test_lose(void) {
    Sim s;
    int guard = 0;
    sim_init(&s);
    sim_place_player(&s, 100, 100);
    sim_place_enemy(&s, 0, 122, 100, 36);
    sim_place_enemy(&s, 1, 0, 0, 0);
    sim_place_enemy(&s, 2, 0, 0, 0);
    while (s.result == 0 && guard < 5000) {
        sim_tick(&s, 0);
        guard++;
    }
    check(s.result == 2, "an enemy who stays in range can defeat the player");
    check(s.player.hp == 0, "defeat means empty health");
}

int main(void) {
    test_move_and_depth();
    test_depth_gate_and_hit();
    test_enemy_aligns_before_strike();
    test_jump();
    test_clear_and_restart();
    test_lose();
    if (g_fail) {
        fprintf(stderr, "%d checks failed\n", g_fail);
        return 1;
    }
    printf("sim ok\n");
    return 0;
}
