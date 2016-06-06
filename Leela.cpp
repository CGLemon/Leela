#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#ifdef USE_CAFFE
#include <glog/logging.h>
#endif
#include "Network.h"

#include "Zobrist.h"
#include "GTP.h"
#include "SMP.h"
#include "Random.h"
#include "Utils.h"
#include "Matcher.h"
#include "AttribScores.h"

using namespace Utils;

#ifdef _CONSOLE
int main (int argc, char *argv[]) {
    int done = false;
    int gtp_mode;
    std::string input;

#ifdef USE_CAFFE
    ::google::InitGoogleLogging(argv[0]);
#endif
    // Defaults
    gtp_mode = false;
    cfg_allow_pondering = true;
    cfg_num_threads = std::min(SMP::get_num_cpus(), MAX_CPUS);
    cfg_enable_nets = true;
    cfg_max_playouts = INT_MAX;

    namespace po = boost::program_options;
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "Show commandline options")
        ("gtp,g", "Enable GTP mode")
        ("threads,t", po::value<int>(), "Number of threads to use")
        ("playouts,p", po::value<int>(), "Limit number of playouts")
        ("noponder", "Disable pondering")
        ("nonets", "Disable use of neural networks")
        ;
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    }  catch(boost::program_options::error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
        return 1;
    }

    // Handle commandline options
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    if (vm.count("gtp")) {
        gtp_mode = true;
    }

    if (vm.count("threads")) {
        int num_threads = vm["threads"].as<int>();
        if (num_threads > cfg_num_threads) {
            std::cerr << "Clamping threads to maximum = " << cfg_num_threads
                      << std::endl;
        } else {
            cfg_num_threads = num_threads;
        }
    }

    if (vm.count("noponder")) {
        cfg_allow_pondering = false;
    }

    if (vm.count("playouts")) {
        cfg_max_playouts = vm["playouts"].as<int>();
    }

    if (vm.count("nonets")) {
        cfg_enable_nets = false;
    }

    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::cin.setf(std::ios::unitbuf);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#ifndef WIN32
    setbuf(stdin, NULL);
#endif

    std::unique_ptr<Random> rng(new Random(5489UL));
    Zobrist::init_zobrist(*rng);

    AttribScores::get_attribscores();
    Matcher::get_Matcher();
    Network::get_Network();

    std::unique_ptr<GameState> maingame(new GameState);

    /* set board limits */
    float komi = 7.5;
    maingame->init_game(19, komi);

    while (!done) {
        if (!gtp_mode) {
            maingame->display_state();
            std::cout << "Leela: ";
        }

        std::getline(std::cin, input);
        GTP::execute(*maingame, input);
    }
    return 0;
}
#endif
