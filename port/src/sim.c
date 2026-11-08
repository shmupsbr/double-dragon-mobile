#include "sim.h"

enum {
    DEPTH_MIN = 36,
    DEPTH_MAX = 168,
    PLAYER_HP = 48,
    ENEMY_HP = 36,
    TIME_START = 60 * 90,
    MEET_X = 22,
    DEPTH_GATE = 16,
    LOCK_X = 210,
    WAVE2_X = 460
};

static void actor_clear(Actor *a) {
    a->x = 0;
    a->depth = 100;
    a->alt = 0;
    a->vy = 0;
    a->facing = 1;
    a->hp = 0;
    a->state = ST_DEAD;
    a->timer = 0;
    a->cooldown = 0;
    a->flash = 0;
    a->hurt_lock = 0;
    a->attack_id = 0;
    a->last_hit_by = 0;
    a->alive = 0;
}

static void begin_attack(Sim *s, Actor *a, int state, int timer) {
    a->state = state;
    a->timer = timer;
    a->attack_id = ++s->next_attack;
}

static void spawn_enemy(Actor *a, int x, int depth) {
    actor_clear(a);
    a->x = x;
    a->depth = depth;
    a->facing = -1;
    a->hp = ENEMY_HP;
    a->state = ST_IDLE;
    a->alive = 1;
    a->cooldown = 20;
}

static void lay_wave(Sim *s, int wave) {
    int base = (wave == 1) ? 300 : s->player.x + 260;
    int depth0 = s->player.depth;
    s->enemy_n = 3;
    spawn_enemy(&s->enemy[0], base, depth0);
    spawn_enemy(&s->enemy[1], base + 70, depth0 - 36);
    spawn_enemy(&s->enemy[2], base + 130, depth0 + 40);
    if (s->enemy[1].depth < DEPTH_MIN) {
        s->enemy[1].depth = DEPTH_MIN;
    }
    if (s->enemy[2].depth > DEPTH_MAX) {
        s->enemy[2].depth = DEPTH_MAX;
    }
    actor_clear(&s->enemy[3]);
}

void sim_init(Sim *s) {
    int i;
    for (i = 0; i < SIM_ENEMIES; i++) {
        actor_clear(&s->enemy[i]);
    }
    actor_clear(&s->player);
    s->player.x = 64;
    s->player.depth = 100;
    s->player.facing = 1;
    s->player.hp = PLAYER_HP;
    s->player.state = ST_IDLE;
    s->player.alive = 1;
    s->wave = 1;
    s->wave2_armed = 0;
    s->camera_x = 0;
    s->camera_locked = 1;
    s->go_timer = 0;
    s->score = 0;
    s->chain = 0;
    s->chain_age = 0;
    s->time_left = TIME_START;
    s->result = 0;
    s->tick = 0;
    s->prev_buttons = 0;
    s->next_attack = 1;
    lay_wave(s, 1);
}

void sim_place_player(Sim *s, int x, int depth) {
    s->player.x = x;
    s->player.depth = depth;
    s->player.state = ST_IDLE;
    s->player.alt = 0;
    s->player.vy = 0;
}

void sim_place_enemy(Sim *s, int index, int x, int depth, int hp) {
    if (index < 0 || index >= SIM_ENEMIES) {
        return;
    }
    spawn_enemy(&s->enemy[index], x, depth);
    s->enemy[index].hp = hp;
    if (hp <= 0) {
        s->enemy[index].alive = 0;
        s->enemy[index].state = ST_DEAD;
    }
}

int sim_living_enemies(const Sim *s) {
    int i, n = 0;
    for (i = 0; i < SIM_ENEMIES; i++) {
        if (s->enemy[i].alive && s->enemy[i].hp > 0 && s->enemy[i].state != ST_DEAD) {
            n++;
        }
    }
    return n;
}

static int overlap(const Actor *a, const Actor *b, int reach) {
    int ax0 = a->x + a->facing * 8;
    int ax1 = ax0 + a->facing * reach;
    int lo = ax0 < ax1 ? ax0 : ax1;
    int hi = ax0 > ax1 ? ax0 : ax1;
    int blo = b->x - 16;
    int bhi = b->x + 16;
    if (hi < blo || lo > bhi) {
        return 0;
    }
    if (b->depth - a->depth > DEPTH_GATE || a->depth - b->depth > DEPTH_GATE) {
        return 0;
    }
    if (b->state == ST_DEAD || !b->alive || b->hp <= 0) {
        return 0;
    }
    return 1;
}

static int active_window(const Actor *a) {
    if (a->state == ST_PUNCH) {
        return a->timer >= 6 && a->timer <= 12;
    }
    if (a->state == ST_KICK) {
        return a->timer >= 8 && a->timer <= 16;
    }
    if (a->state == ST_SPECIAL) {
        return a->timer >= 6 && a->timer <= 14;
    }
    return 0;
}

static int attack_reach(const Actor *a) {
    if (a->state == ST_KICK) {
        return 34;
    }
    if (a->state == ST_SPECIAL) {
        return 22;
    }
    return 26;
}

static int attack_damage(const Actor *a) {
    if (a->state == ST_KICK) {
        return 12;
    }
    if (a->state == ST_SPECIAL) {
        return 16;
    }
    return 8;
}

static void suffer(Sim *s, Actor *victim, Actor *src, int dmg, int knock) {
    if (victim->hurt_lock > 0 || victim->state == ST_DEAD || victim->hp <= 0) {
        return;
    }
    if (victim->last_hit_by == src->attack_id) {
        return;
    }
    victim->last_hit_by = src->attack_id;
    victim->hp -= dmg;
    victim->flash = 8;
    victim->facing = src->x < victim->x ? -1 : 1;
    if (victim == &s->player) {
        s->chain = 0;
        s->chain_age = 0;
    } else {
        if (s->chain_age > 0 && s->chain_age < 50) {
            s->chain += 1;
        } else {
            s->chain = 1;
        }
        s->chain_age = 1;
        s->score += 100 * s->chain;
    }
    if (victim->hp <= 0) {
        victim->hp = 0;
        victim->state = ST_DOWN;
        victim->timer = 36;
        victim->alt = 0;
        victim->vy = 0;
        return;
    }
    victim->state = knock ? ST_DOWN : ST_HURT;
    victim->timer = knock ? 28 : 14;
    victim->hurt_lock = 18;
    victim->x += src->facing * 10;
    if (victim->x < 16) {
        victim->x = 16;
    }
}

static void finish_state(Actor *a) {
    if (a->timer > 0) {
        a->timer -= 1;
        return;
    }
    if (a->state == ST_DOWN) {
        if (a->hp <= 0) {
            a->state = ST_DEAD;
            a->alive = 0;
        } else {
            a->state = ST_IDLE;
        }
        return;
    }
    if (a->state == ST_PUNCH || a->state == ST_KICK || a->state == ST_SPECIAL ||
        a->state == ST_HURT) {
        a->state = ST_IDLE;
    }
}

static void step_air(Actor *a) {
    if (a->state != ST_AIR) {
        return;
    }
    a->alt += a->vy;
    a->vy -= 1;
    if (a->alt <= 0) {
        a->alt = 0;
        a->vy = 0;
        a->state = ST_IDLE;
    }
}

static void move_player(Sim *s, unsigned buttons) {
    Actor *p = &s->player;
    int edge;
    if (!p->alive || p->hp <= 0 || s->result) {
        return;
    }
    if (p->flash > 0) {
        p->flash -= 1;
    }
    if (p->hurt_lock > 0) {
        p->hurt_lock -= 1;
    }
    if (p->state == ST_AIR) {
        step_air(p);
        if (buttons & SIM_LEFT) {
            p->x -= 2;
            p->facing = -1;
        } else if (buttons & SIM_RIGHT) {
            p->x += 2;
            p->facing = 1;
        }
        if (p->x < 24) {
            p->x = 24;
        }
        return;
    }
    if (p->state == ST_PUNCH || p->state == ST_KICK || p->state == ST_SPECIAL ||
        p->state == ST_HURT || p->state == ST_DOWN) {
        finish_state(p);
        return;
    }
    edge = (buttons & ~s->prev_buttons);
    if (edge & SIM_PUNCH) {
        begin_attack(s, p, ST_PUNCH, 18);
        return;
    }
    if (edge & SIM_KICK) {
        begin_attack(s, p, ST_KICK, 22);
        return;
    }
    if (edge & SIM_SPECIAL) {
        begin_attack(s, p, ST_SPECIAL, 20);
        return;
    }
    if (edge & SIM_JUMP) {
        p->state = ST_AIR;
        p->vy = 7;
        p->alt = 0;
        return;
    }
    p->state = ST_IDLE;
    if (buttons & SIM_LEFT) {
        p->x -= 3;
        p->facing = -1;
        p->state = ST_WALK;
    } else if (buttons & SIM_RIGHT) {
        p->x += 3;
        p->facing = 1;
        p->state = ST_WALK;
    }
    if (buttons & SIM_UP) {
        p->depth -= 2;
        p->state = ST_WALK;
    } else if (buttons & SIM_DOWN) {
        p->depth += 2;
        p->state = ST_WALK;
    }
    if (p->depth < DEPTH_MIN) {
        p->depth = DEPTH_MIN;
    }
    if (p->depth > DEPTH_MAX) {
        p->depth = DEPTH_MAX;
    }
    if (p->x < 24) {
        p->x = 24;
    }
    if (s->wave == 1 && s->camera_locked && p->x > LOCK_X) {
        p->x = LOCK_X;
    }
    if (p->x > 1400) {
        p->x = 1400;
    }
}

static void move_enemy(Sim *s, Actor *e) {
    Actor *p = &s->player;
    int dx, dd;
    if (!e->alive || e->hp <= 0 || e->state == ST_DEAD) {
        return;
    }
    if (e->flash > 0) {
        e->flash -= 1;
    }
    if (e->hurt_lock > 0) {
        e->hurt_lock -= 1;
    }
    if (e->cooldown > 0) {
        e->cooldown -= 1;
    }
    if (e->state == ST_PUNCH || e->state == ST_HURT || e->state == ST_DOWN) {
        finish_state(e);
        return;
    }
    if (!p->alive || s->result) {
        e->state = ST_IDLE;
        return;
    }
    dx = p->x - e->x;
    dd = p->depth - e->depth;
    e->facing = dx >= 0 ? 1 : -1;
    /* Match the player's depth before stepping in to strike. */
    if (dd > 8 || dd < -8) {
        e->depth += (dd > 0) ? 2 : -2;
        if (e->depth < DEPTH_MIN) {
            e->depth = DEPTH_MIN;
        }
        if (e->depth > DEPTH_MAX) {
            e->depth = DEPTH_MAX;
        }
        e->state = ST_WALK;
        return;
    }
    if (dx > MEET_X || dx < -MEET_X) {
        e->x += (dx > 0) ? 2 : -2;
        e->state = ST_WALK;
        return;
    }
    if (e->cooldown == 0 && p->state != ST_DOWN && p->hp > 0) {
        begin_attack(s, e, ST_PUNCH, 20);
        e->cooldown = 46;
        return;
    }
    e->state = ST_IDLE;
}

static void resolve_attacks(Sim *s) {
    int i;
    Actor *p = &s->player;
    if (p->alive && active_window(p)) {
        for (i = 0; i < SIM_ENEMIES; i++) {
            Actor *e = &s->enemy[i];
            if (!e->alive) {
                continue;
            }
            if (overlap(p, e, attack_reach(p))) {
                int knock = (p->state == ST_KICK || p->state == ST_SPECIAL || e->hp <= attack_damage(p));
                suffer(s, e, p, attack_damage(p), knock);
            }
        }
    }
    for (i = 0; i < SIM_ENEMIES; i++) {
        Actor *e = &s->enemy[i];
        if (!e->alive || !active_window(e)) {
            continue;
        }
        if (p->alive && overlap(e, p, attack_reach(e))) {
            suffer(s, p, e, attack_damage(e), 0);
        }
    }
}

static void update_progress(Sim *s) {
    if (s->result) {
        return;
    }
    if (s->player.hp <= 0) {
        s->result = 2;
        s->camera_locked = 1;
        return;
    }
    if (s->time_left > 0) {
        s->time_left -= 1;
    } else {
        s->result = 2;
        return;
    }
    if (s->chain_age > 0) {
        s->chain_age += 1;
        if (s->chain_age > 50) {
            s->chain = 0;
            s->chain_age = 0;
        }
    }
    if (s->wave == 1 && sim_living_enemies(s) == 0) {
        s->camera_locked = 0;
        if (s->go_timer < 90) {
            s->go_timer += 1;
        }
        if (!s->wave2_armed && s->player.x >= WAVE2_X) {
            s->wave = 2;
            s->wave2_armed = 1;
            s->go_timer = 0;
            s->camera_locked = 0;
            lay_wave(s, 2);
        }
    }
    if (s->wave == 2 && s->wave2_armed && sim_living_enemies(s) == 0) {
        s->result = 1;
        s->camera_locked = 1;
    }
    if (!s->camera_locked) {
        s->camera_x = s->player.x - 110;
        if (s->camera_x < 0) {
            s->camera_x = 0;
        }
    } else if (s->wave == 1) {
        s->camera_x = 0;
    }
}

void sim_tick(Sim *s, unsigned buttons) {
    int i;
    if (buttons & SIM_RESTART) {
        if (s->result || (buttons & SIM_RESTART)) {
            /* Restart is always available. The end cards mention it. */
            if ((buttons & ~s->prev_buttons) & SIM_RESTART) {
                sim_init(s);
                s->prev_buttons = buttons;
                return;
            }
        }
    }
    if (!s->result) {
        move_player(s, buttons);
        for (i = 0; i < SIM_ENEMIES; i++) {
            move_enemy(s, &s->enemy[i]);
        }
        resolve_attacks(s);
        update_progress(s);
    }
    s->prev_buttons = buttons;
    s->tick += 1;
}
