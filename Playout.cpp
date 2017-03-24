#include <vector>
#include <array>
#include <cstdlib>
#include <cassert>
#include <thread>
#include <algorithm>
#include "config.h"

#include "Timing.h"
#include "GameState.h"
#include "Playout.h"
#include "Utils.h"
#include "MCOTable.h"
#include "Random.h"
#include "GTP.h"

using namespace Utils;

Playout::Playout() :
    m_run(false), m_eval_valid(false) {
    m_sq[0].reset();
    m_sq[1].reset();
}

float Playout::get_score() {
    assert(m_run);
    assert(m_score > -2.00f && m_score < 2.00f);

    return m_score;
}

float Playout::get_territory() {
    assert(m_run);
    return m_territory;
}

void Playout::set_eval( float eval) {
    m_blackeval = eval;
    m_eval_valid = true;
}

float Playout::get_eval() {
    assert(m_eval_valid == true);
    return m_blackeval;
}

bool Playout::has_eval() {
    return m_eval_valid;
}

void Playout::run(FastState & state, bool postpassout, bool resigning) {
    assert(!m_run);

    const int boardsize = state.board.get_boardsize();

    const int resign = (boardsize * boardsize) / 3;
    const int playoutlen = (boardsize * boardsize) * 2;

    // 2 passes end the game, except when we're marking
    const int maxpasses = postpassout ? 4 : 2;

    int counter = 0;

    // do the main loop
    while (state.get_passes() < maxpasses
        && state.get_movenum() < playoutlen
        && (!resigning || abs(state.estimate_mc_score()) < resign)) {
        int vtx = state.play_random_move();

        if (counter < 30 && vtx != FastBoard::PASS) {
            int color = !state.get_to_move();

            if (!m_sq[!color][vtx]) {
                m_sq[color][vtx] = true;
            }
        }

        counter++;
    }

    // get ownership info
    bitboard_t blackowns;

    for (int i = 0; i < boardsize; i++) {
        for (int j = 0; j < boardsize; j++) {
            int vtx = state.board.get_vertex(i, j);
            if (state.board.get_square(vtx) == FastBoard::BLACK) {
                blackowns[vtx] = true;
            } else if (state.board.get_square(vtx) == FastBoard::EMPTY) {
                if (state.board.is_eye(FastBoard::BLACK, vtx)) {
                    blackowns[vtx] = true;
                }
            }
        }
    }

    float score = state.calculate_mc_score();

    // update MCO in one swoop
    bool blackwon;
    if (score == 0.0f) {
        blackwon = (Random::get_Rng()->randint(2) == 0);
    } else {
        blackwon = (score > 0.0f);
    }
    MCOwnerTable::get_MCO()->update_owns(blackowns, blackwon);

    m_run = true;
    m_territory = score;
    // Scale to -1.0 <--> 1.0
    m_score = score / (boardsize * boardsize);
}

bool Playout::passthrough(int color, int vertex) {
    assert(m_run);
    
    if (vertex == FastBoard::PASS) {
        return false;
    }
    
    return m_sq[color][vertex];
}

void Playout::do_playout_benchmark(GameState & game) {   
    float ftmp;
    int loop;    
    float len;
    float score;
    const int boardsize = game.board.get_boardsize();
    const int resign = (boardsize * boardsize) / 3;
    const int playoutlen = (boardsize * boardsize) * 2;    
    
    len = 0.0;
    score = 0;
    Time start;
    
    for (loop = 0; loop < AUTOGAMES; loop++) {
        do {                                    
            game.play_random_move();
            
        } while (game.get_passes() < 2 
                 && game.get_movenum() < playoutlen
                 && abs(game.estimate_mc_score()) < resign); 
                
        len += game.get_movenum();
        ftmp = game.calculate_mc_score();   
        score += ftmp;                
                
        game.reset_game();
    }
    
    Time end;
    
    myprintf("%d games in %5.2f seconds -> %d g/s\n", 
            AUTOGAMES, 
            (float)Time::timediff(start,end)/100.0, 
            (int)((float)AUTOGAMES/((float)Time::timediff(start,end)/100.0)));
    myprintf("Avg Len: %5.2f Score: %f\n", len/(float)AUTOGAMES, score/AUTOGAMES);
}

float Playout::mc_owner(FastState & state, int iterations, float* points) {
    int cpus = cfg_num_threads;
    int iters_per_thread = (iterations + (cpus - 1)) / cpus;

    std::atomic<float> bwins{0.0f};
    std::atomic<float> board_score{0.0f};

    std::vector<std::thread> tg;
    for (int i = 0; i < cpus; i++) {
        tg.push_back(
            std::thread([iters_per_thread, &state,
                         &bwins, &board_score]() {
                float thread_bwins = 0.0f;
                float thread_board_score = 0.0f;
                for (int i = 0; i < iters_per_thread; i++) {
                    FastState tmp = state;

                    Playout p;
                    p.run(tmp, true, false);

                    float score = p.get_score();
                    if (score == 0.0f) {
                        thread_bwins += 0.5f;
                    } else if (score > 0.0f) {
                        thread_bwins += 1.0f;
                    }
                    thread_board_score += p.get_territory();
                }
                atomic_add(bwins, thread_bwins);
                atomic_add(board_score, thread_board_score);
            }
        ));
    }

    auto join_thread = [](std::thread &thread) {
        assert(thread.joinable());
        thread.join();
    };
    std::for_each(tg.begin(), tg.end(), join_thread);

    float score = bwins / (float)iterations;
    float territory = board_score / (float)iterations;

    if (state.get_to_move() != FastBoard::BLACK) {
        score = 1.0f - score;
    }

    if (points != nullptr) {
        *points = territory;
    }

    return score;
}
