#include "replay.h"

#include <mkb.h>

#include "draw.h"
#include "heap.h"
#include "macro_utils.h"
#include "memstore.h"
#include "pad.h"
#include "patch.h"
#include "pref.h"
#include "timer.h"

namespace replay {

struct S8Vec {
    s8 x, y;
};

struct SaveState {
    bool active;
    s32 stage_id;
    u8 character;
    memstore::MemStore store;
    u8 pause_menu_sprite_status;
    mkb::Sprite pause_menu_sprite;
};

// This could be halve as large by enumerating possible inputs instead
static S8Vec s_possible_inputs[121 * 121];

static SaveState s_state;

// For all memory regions that involve just saving/loading to the same region...
// Do a pass over them. This may involve preallocating a buffer to save them in, actually saving
// them, or restoring them, depending on the mode `memStore` is in
static void pass_over_regions(memstore::MemStore* store) {
    store->do_region(&mkb::balls[0], sizeof(mkb::balls[0]));
    store->do_region(&mkb::sub_mode, sizeof(mkb::sub_mode));
    store->do_region(&mkb::mode_info.stage_time_frames_remaining,
                     sizeof(mkb::mode_info.stage_time_frames_remaining));
    store->do_region(reinterpret_cast<void*>(0x8054E03C), 0xe0);  // Camera region
    store->do_region(reinterpret_cast<void*>(0x805BD830), 0x1c);  // Some physics region
    store->do_region(&mkb::mode_info.g_ball_mode, sizeof(mkb::mode_info.g_ball_mode));
    store->do_region(mkb::g_camera_standstill_counters, sizeof(mkb::g_camera_standstill_counters));
    store->do_region(mkb::balls[0].ape,
                     sizeof(*mkb::balls[0].ape));  // Store entire ape struct for now

    // Itemgroups
    store->do_region(mkb::itemgroups, sizeof(mkb::Itemgroup) * mkb::stagedef->coli_header_count);

    // Bananas
    store->do_region(&mkb::items, sizeof(mkb::Item) * mkb::stagedef->banana_count);

    // Goal tape, party ball, and button stage objects
    for (u32 i = 0; i < mkb::stobj_pool_info.upper_bound; i++) {
        if (mkb::stobj_pool_info.status_list[i] == 0) continue;

        switch (mkb::stobjs[i].type) {
            case mkb::STOBJ_GOALTAPE:
            case mkb::STOBJ_GOALBAG:
            case mkb::STOBJ_GOALBAG_EXMASTER:
            case mkb::STOBJ_BUTTON: {
                store->do_region(&mkb::stobjs[i], sizeof(mkb::stobjs[i]));
                break;
            }
            default:
                break;
        }
    }

    // Seesaws
    for (u32 i = 0; i < mkb::stagedef->coli_header_count; i++) {
        if (mkb::stagedef->coli_header_list[i].anim_loop_type_and_seesaw == mkb::ANIM_SEESAW) {
            store->do_region(mkb::itemgroups[i].seesaw_info->state, 12);
        }
    }

    // Goal tape and party ball-specific extra data
    store->do_region(mkb::goaltapes, sizeof(mkb::GoalTape) * mkb::stagedef->goal_count);
    store->do_region(mkb::goalbags, sizeof(mkb::GoalBag) * mkb::stagedef->goal_count);

    // Pause menu
    store->do_region(reinterpret_cast<void*>(0x8054DCA8), 56);  // Pause menu state
    store->do_region(reinterpret_cast<void*>(0x805BC474), 4);   // Pause menu bitfield

    for (u32 i = 0; i < mkb::sprite_pool_info.upper_bound; i++) {
        if (mkb::sprite_pool_info.status_list[i] == 0) continue;
        mkb::Sprite* sprite = &mkb::sprites[i];

        if (sprite->tick_func == mkb::sprite_timer_ball_tick) {
            // Timer ball sprite (it'll probably always be in the same place in the sprite array)
            store->do_region(sprite, sizeof(*sprite));
        } else if (sprite->tick_func == mkb::sprite_score_tick) {
            // Score sprite's lerped score value
            store->do_region(&sprite->fpara1, sizeof(sprite->fpara1));
        }
    }

    // RTA timer
    timer::save_state(store);
}

static void handle_pause_menu_save(SaveState* state) {
    state->pause_menu_sprite_status = 0;

    // Look for an active sprite that has the same dest func pointer as the pause menu sprite
    for (u32 i = 0; i < mkb::sprite_pool_info.upper_bound; i++) {
        if (mkb::sprite_pool_info.status_list[i] == 0) continue;

        mkb::Sprite& sprite = mkb::sprites[i];
        if (sprite.disp_func == mkb::sprite_pausemenu_disp) {
            state->pause_menu_sprite_status = mkb::sprite_pool_info.status_list[i];
            state->pause_menu_sprite = sprite;

            break;
        }
    }
}

static void handle_pause_menu_load(SaveState* state) {
    bool paused_now = mkb::g_some_other_flags & mkb::OF_GAME_PAUSED;
    bool paused_in_state = state->pause_menu_sprite_status != 0;

    if (paused_now && !paused_in_state) {
        // Destroy the pause menu sprite that currently exists
        for (u32 i = 0; i < mkb::sprite_pool_info.upper_bound; i++) {
            if (mkb::sprite_pool_info.status_list[i] == 0) continue;

            if (reinterpret_cast<u32>(mkb::sprites[i].disp_func) == 0x8032a4bc) {
                mkb::sprite_pool_info.status_list[i] = 0;
                break;
            }
        }
    } else if (!paused_now && paused_in_state) {
        // Allocate a new pause menu sprite
        s32 i = mkb::pool_alloc(&mkb::sprite_pool_info, state->pause_menu_sprite_status);
        mkb::sprites[i] = state->pause_menu_sprite;
    }
}

static void destruct_non_gameplay_sprites() {
    for (u32 i = 0; i < mkb::sprite_pool_info.upper_bound; i++) {
        if (mkb::sprite_pool_info.status_list[i] == 0) continue;

        mkb::Sprite* sprite = &mkb::sprites[i];
        bool post_goal_sprite = (sprite->disp_func == mkb::sprite_goal_disp ||
                                 sprite->disp_func == mkb::sprite_clear_score_disp ||
                                 sprite->disp_func == mkb::sprite_warp_bonus_disp ||
                                 sprite->disp_func == mkb::sprite_time_bonus_disp ||
                                 sprite->disp_func == mkb::sprite_stage_score_disp ||
                                 sprite->tick_func == mkb::sprite_fallout_tick ||
                                 sprite->tick_func == mkb::sprite_bonus_finish_or_perfect_tick ||
                                 sprite->tick_func == mkb::sprite_ready_tick ||
                                 sprite->tick_func == mkb::sprite_go_tick ||
                                 sprite->tick_func == mkb::sprite_player_num_tick ||
                                 sprite->tick_func == mkb::sprite_replay_tick ||
                                 sprite->tick_func == mkb::sprite_loadin_stage_name_tick ||
                                 sprite->tick_func == mkb::sprite_bonus_stage_tick ||
                                 sprite->tick_func == mkb::sprite_final_stage_tick);
        if (post_goal_sprite) mkb::sprite_pool_info.status_list[i] = 0;
    }
}

static void destruct_distracting_effects() {
    // Destruct current spark effects so we don't see big sparks
    // generated when changing position by a large amount.
    // Also destruct banana grabbing effects
    for (u32 i = 0; i < mkb::effect_pool_info.upper_bound; i++) {
        if (mkb::effect_pool_info.status_list[i] == 0) continue;

        switch (mkb::effects[i].type) {
            case mkb::EFFECT_COLI_PARTICLE:
            case mkb::EFFECT_HOLDING_BANANA:
            case mkb::EFFECT_GET_BANANA: {
                mkb::effect_pool_info.status_list[i] = 0;
            }
            default:
                break;
        }
    }
}

static void save_state() {
    s_state.store.enter_prealloc_mode();
    pass_over_regions(&s_state.store);
    MOD_ASSERT(s_state.store.enter_save_mode());

    s_state.active = true;
    s_state.stage_id = mkb::current_stage_id;
    s_state.character = mkb::selected_characters[mkb::curr_player_idx];
    pass_over_regions(&s_state.store);

    handle_pause_menu_save(&s_state);
}

static void load_state() {
    // Need to handle pausemenu-specific loading first so we can detect the game isn't currently
    // paused
    handle_pause_menu_load(&s_state);

    s_state.store.enter_load_mode();
    pass_over_regions(&s_state.store);

    destruct_non_gameplay_sprites();
    destruct_distracting_effects();
}

static u32 s8vec_dist_sq(const S8Vec& pt1, const S8Vec& pt2) {
    s32 delta_x = static_cast<s32>(pt1.x) - static_cast<s32>(pt2.x);
    s32 delta_y = static_cast<s32>(pt1.y) - static_cast<s32>(pt2.y);
    return delta_x * delta_x + delta_y * delta_y;
}

static S8Vec s_comparison_pt;

static void sort_inputs_by_dist(const S8Vec& comparison_pt) {
    s_comparison_pt = comparison_pt;
    mkb::qsort(s_possible_inputs, LEN(s_possible_inputs), sizeof(s_possible_inputs[0]),
               [](void* pt1_void, void* pt2_void) {
                   S8Vec* pt1 = static_cast<S8Vec*>(pt1_void);
                   S8Vec* pt2 = static_cast<S8Vec*>(pt2_void);
                   u32 dist1 = s8vec_dist_sq(*pt1, s_comparison_pt);
                   u32 dist2 = s8vec_dist_sq(*pt2, s_comparison_pt);

                   if (dist1 < dist2) return -1;
                   if (dist1 > dist2) return 1;
                   return 0;
               });
}

void init() {
    for (u32 i = 0; i < LEN(s_possible_inputs); i++) {
        s8 x = static_cast<s8>(i % 121 - 61);
        s8 y = static_cast<s8>(i / 121 - 61);
        s_possible_inputs[i] = {x, y};
    }

    sort_inputs_by_dist({0, 0});

    for (u32 i = 0; i < LEN(s_possible_inputs); i++) {
        mkb::OSReport("Possible input: (%d, %d)\n", s_possible_inputs[i].x, s_possible_inputs[i].y);
    }
}

void tick() {
    if (pad::button_pressed(mkb::PAD_BUTTON_X)) {
        save_state();
    }
    if (pad::button_pressed(mkb::PAD_BUTTON_Y)) {
        load_state();
    }
}

}  // namespace replay
