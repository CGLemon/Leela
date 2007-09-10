#include <iostream>

#include "config.h"

#include "Zobrist.h"
#include "GTP.h"
#include "Random.h"
#include "Utils.h"

using namespace Utils;

int main (int argc, char *argv[]) {        
    int done = false;
    int gtp_mode;
    std::string input;      
    
    /* default to prompt */
    gtp_mode = false;
    
    if (argc > 1) {
        gtp_mode = true;
    }   

    std::cout.setf(std::ios::unitbuf);
    std::cin.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);    
    
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);
    setbuf(stderr, NULL);    
                    
    std::auto_ptr<Random> rng(new Random(5489UL));          
    Zobrist::init_zobrist(*rng);
    
    std::auto_ptr<GameState> maingame(new GameState);    
        
    /* set board limits */    
    float komi = 7.5;         
    maingame->init_game(9, komi);
            
    while (!done) {
        if (!gtp_mode) {
            maingame->display_state();
            myprintf("Ka: ");
        }            
                
        std::getline(std::cin, input);       
        
        if (!GTP::execute(*maingame, input)) {
            myprintf("? unknown command\n");                   
        } 
    }    
    return 0;
}

