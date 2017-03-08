#include "config.h"

#include <memory>
#include <cmath>
#include <unordered_set>
#include <string>
#include <iostream>
#include <fstream>
#include <omp.h>

#include "MCPolicy.h"
#include "SGFParser.h"
#include "SGFTree.h"
#include "Utils.h"
#include "Random.h"
#include "Network.h"
#include "Playout.h"

using namespace Utils;

std::unordered_map<int, float> PolicyWeights::pattern_weights;
std::array<float, NUM_FEATURES> PolicyWeights::feature_weights{
    0.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f
};
std::unordered_map<int, float> PolicyWeights::pattern_gradients;
std::array<float, NUM_FEATURES> PolicyWeights::feature_gradients{};

void MCPolicy::mse_from_file(std::string filename) {
    std::vector<std::string> games = SGFParser::chop_all(filename);
    size_t gametotal = games.size();
    myprintf("Total games in file: %d\n", gametotal);

    double sum_sq_pp = 0.0;
    double sum_sq_nn = 0.0;
    int count = 0;

    while (1) {
        int pick = Random::get_Rng()->randint32(gametotal);

        std::unique_ptr<SGFTree> sgftree(new SGFTree);
        try {
            sgftree->load_from_string(games[pick]);
        } catch (...) {
        };

        int who_won = sgftree->get_winner();
        int handicap = sgftree->get_state()->get_handicap();

        int movecount = sgftree->count_mainline_moves();
        int move_pick = Random::get_Rng()->randint32(movecount);
        // GameState state = sgftree->follow_mainline_state(move_pick);
        KoState * state = sgftree->get_state_from_mainline(move_pick);

        if (who_won != FastBoard::BLACK && who_won != FastBoard::WHITE) {
            continue;
        }
        bool blackwon = (who_won == FastBoard::BLACK);

        PolicyWeights::feature_gradients.fill(0.0f);
        constexpr int iterations = 128;
        float bwins = 0.0f;

        #pragma omp parallel
        {
            // Get EV (V)
            #pragma omp for reduction (+:bwins)
            for (int i = 0; i < iterations; i++) {
                FastState tmp = *state;

                Playout p;
                p.run(tmp, false, true, nullptr);

                float score = p.get_score();
                if (score > 0.0f) {
                    bwins += 1.0f;
                }
            }

            // Policy Trace per thread
            #pragma omp for
            for (int i = 0; i < iterations; i++) {
                FastState tmp = *state;

                PolicyTrace policy_trace;
                Playout p;
                p.run(tmp, false, true, &policy_trace);

                bool black_won = p.get_score() > 0.0f;
                policy_trace.trace_process(iterations, black_won);
            }
        }

        bwins /= (float)iterations;
        MCPolicy::adjust_weights(blackwon, bwins);

        float nwscore = Network::get_Network()->get_value(
            state, Network::Ensemble::RANDOM_ROTATION);

        if (state->get_to_move() == FastBoard::WHITE) {
            nwscore = 1.0f - nwscore;
        }

       // myprintf("n=%d BW: %d Score: %1.4f NN: %1.4f ",
       //          count, blackwon, bwins, nwscore);

        sum_sq_pp += std::pow(2.0f*((blackwon ? 1.0f : 0.0f) - bwins),   2.0f);
        sum_sq_nn += std::pow(2.0f*((blackwon ? 1.0f : 0.0f) - nwscore), 2.0f);

        count++;

        if (count % 1000 == 0) {
            myprintf("n=%d MSE MC=%1.4f MSE NN=%1.4f\n",
                count,
                sum_sq_pp/((double)2.0*count),
                sum_sq_nn/((double)2.0*count));
        }
        if (count % 1000 == 0) {
            std::string filename = "rltune_" + std::to_string(count) + ".txt";
            std::ofstream out(filename);
            for (int w = 0; w < NUM_FEATURES; w++) {
                out << w << " = " << PolicyWeights::feature_weights[w] << std::endl;
            }
            for (auto & pat : PolicyWeights::pattern_weights) {
                out << "{ " << pat.first << ", " << pat.second << "}, " << std::endl;;
            }
            out.close();
        }
    }
}

void PolicyTrace::trace_process(int iterations, bool blackwon) {
    std::vector<float> policy_feature_gradient;
    policy_feature_gradient.resize(NUM_FEATURES);
    std::unordered_map<int, float> policy_pattern_gradient;

    if (trace.empty()) return;

    for (auto & decision : trace) {
        float sign = 1.0f;
        if (decision.black_to_move != blackwon) {
            sign = -1.0f;
        }
        std::vector<float> candidate_scores;
        candidate_scores.reserve(decision.candidates.size());
        // get real probabilities
        float sum_scores = 0.0f;
        for (auto & mwf : decision.candidates) {
            float score = mwf.get_score();
            sum_scores += score;
            assert(!std::isnan(score));
            candidate_scores.push_back(score);
        }
        assert(sum_scores > 0.0f);
        assert(!std::isnan(sum_scores));
        std::vector<float> candidate_probabilities;
        candidate_probabilities.resize(candidate_scores.size());
        for (int i = 0; i < candidate_scores.size(); i++) {
            candidate_probabilities[i] = candidate_scores[i] / sum_scores;
        }

        // loop over features, get prob of feature
        std::vector<float> feature_probabilities;
        feature_probabilities.reserve(NUM_FEATURES);
        for (int i = 0; i < NUM_FEATURES; i++) {
            float weight_prob = 0.0f;
            for (int c = 0; c < candidate_probabilities.size(); c++) {
                if (decision.candidates[c].has_bit(i)) {
                    weight_prob += candidate_probabilities[c];
                }
            }
            assert(!std::isnan(weight_prob));
            feature_probabilities.push_back(weight_prob);
        }

        // now deal with patterns
        // get all the ones that occur into a simple list
        std::unordered_set<int> seen_patterns;
        for (auto & mwf : decision.candidates) {
            if (!mwf.is_pass()) {
                seen_patterns.insert(mwf.get_pattern());
            }
        }
        std::vector<int> patterns(seen_patterns.begin(), seen_patterns.end());

        // get pat probabilities
        std::vector<float> pattern_probabilities;
        pattern_probabilities.reserve(patterns.size());
        for (auto & pat : patterns) {
            float weight_prob = 0.0f;
            for (int c = 0; c < candidate_probabilities.size(); c++) {
                if (!decision.candidates[c].is_pass()) {
                    if (decision.candidates[c].get_pattern() == pat) {
                        weight_prob += candidate_probabilities[c];
                    }
                }
            }
            pattern_probabilities.push_back(weight_prob);
        }

        // get policy gradient
        for (int i = 0; i < NUM_FEATURES; i++) {
            float observed = 0.0f;
            if (decision.pick.has_bit(i)) {
                observed = 1.0f;
            }
            policy_feature_gradient[i] += sign *
                (observed - feature_probabilities[i]);
            assert(!std::isnan(policy_feature_gradient[i]));
        }

        for (int i = 0; i < patterns.size(); i++) {
            float observed = 0.0f;
            if (!decision.pick.is_pass()) {
                if (decision.pick.get_pattern() == patterns[i]) {
                    observed = 1.0f;
                }
            }
            policy_pattern_gradient[patterns[i]] += sign *
                (observed - pattern_probabilities[i]);
            assert(!std::isnan(policy_pattern_gradient[i]));
        }
    }

    float positions = trace.size();
    float iters = iterations;
    assert(positions > 0.0f && iters > 0.0f);

    for (int i = 0; i < NUM_FEATURES; i++) {
        // scale by N*T
        policy_feature_gradient[i] /= positions * iters;
        // accumulate total
        #pragma omp critical(feature_gradients)
        PolicyWeights::feature_gradients[i] += policy_feature_gradient[i];
    }

    for (auto & pat : policy_pattern_gradient) {
        pat.second /= positions * iters;
        #pragma omp critical(pattern_gradients)
        PolicyWeights::pattern_gradients[pat.first] += pat.second;
    }
}

void MCPolicy::adjust_weights(bool blackwon, float black_winrate) {
    constexpr float alpha = 0.1f;
    float Vstar = (blackwon ? 1.0f : 0.0f);
    float Vdelta = Vstar - black_winrate;

    for (int i = 0; i < NUM_FEATURES; i++) {
        float orig_weight = PolicyWeights::feature_weights[i];
        float gradient = PolicyWeights::feature_gradients[i];
        // Convert to theta
        float theta = std::log(orig_weight);
        theta += alpha * Vdelta * gradient;
        float gamma = std::exp(theta);
        assert(!std::isnan(gamma));
        gamma = std::max(gamma, 1e-5f);
        gamma = std::min(gamma, 1e5f);
        PolicyWeights::feature_weights[i] = gamma;
    }

    for (auto & pat : PolicyWeights::pattern_gradients) {
        float orig_weight = PolicyWeights::get_pattern_weight(pat.first);
        float gradient = pat.second;
        // Convert to theta
        float theta = std::log(orig_weight);
        theta += alpha * Vdelta * gradient;
        float gamma = std::exp(theta);
        assert(!std::isnan(gamma));
        gamma = std::max(gamma, 1e-5f);
        gamma = std::min(gamma, 1e5f);
        PolicyWeights::set_pattern_weight(pat.first, gamma);
    }
}
