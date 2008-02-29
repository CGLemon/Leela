#include <vector>

#include "FastState.h"
#include "FastBoard.h"
#include "PNSearch.h"
#include "Utils.h"

using namespace Utils;

PNSearch::PNSearch(KoState & ks) 
    : m_rootstate(ks) {    
}

void PNSearch::classify_groups() {    
    int size = m_rootstate.board.get_boardsize();

    // Need separate "seen" and "unknown" markers to
    // prevent rescanning unknown groups
    std::vector<bool> groupmarker(FastBoard::MAXSQ, false);
    std::vector<status_t> groupstatus(FastBoard::MAXSQ, UNKNOWN);

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int vtx = m_rootstate.board.get_vertex(i, j);
            int sq = m_rootstate.board.get_square(vtx);

            if (sq < FastBoard::EMPTY) {
                int par = m_rootstate.board.get_groupid(vtx);
                if (!groupmarker[par]) {                                    
                    if (m_rootstate.board.string_size(par) > 1) {
                        status_t status = check_group(par);
                        groupstatus[par] = status;
                        groupmarker[par] = true;

                        // find the entire chain for this stone
                        std::string chain_list;
                        std::vector<int> chains;
                        m_rootstate.board.augment_chain(chains, vtx);                        

                        // mark the entire augmented chain
                        for (int i = 0; i < chains.size(); i++) {
                            groupstatus[chains[i]] = status;
                            groupmarker[chains[i]] = true;
                            std::string groupname = m_rootstate.board.move_to_text(chains[i]);
                            chain_list.append(groupname);
                            chain_list.append(" ");                            
                        }
                        myprintf("Marking %sas seen\n", chain_list.c_str());
                    }                    
                }                
            }
        }
    }
}

PNSearch::status_t PNSearch::check_group(int groupid) {
    std::string groupname = m_rootstate.board.move_to_text(groupid);
    myprintf("Scanning group %s\n", groupname.c_str());

    m_root.reset(new PNNode());        

    m_group_to_check = groupid;
    m_group_color = m_rootstate.board.get_square(groupid);            
    int rootcolor = m_rootstate.get_to_move();  

    // determine Region Of Interest
    std::vector<bool> roi_map(m_rootstate.board.m_maxsq, false);
    
    std::vector<int> stones = m_rootstate.board.get_augmented_string(groupid);
    std::vector<int> libs_1 = m_rootstate.board.dilate_liberties(stones);
    std::vector<int> libs_2 = m_rootstate.board.dilate_liberties(libs_1);    

    std::vector<int> attackers = m_rootstate.board.get_nearby_enemies(stones);
    std::vector<int> defenders = m_rootstate.board.get_nearby_enemies(attackers);

    std::vector<int> attack_libs = m_rootstate.board.dilate_liberties(attackers);
    std::vector<int> defend_libs = m_rootstate.board.dilate_liberties(defenders);

    std::vector<int> roi;
    
    std::copy(libs_2.begin(), libs_2.end(),           back_inserter(roi));
    std::copy(attack_libs.begin(), attack_libs.end(), back_inserter(roi));
    std::copy(defend_libs.begin(), defend_libs.end(), back_inserter(roi));

    std::sort(roi.begin(), roi.end());    
    roi.erase(std::unique(roi.begin(), roi.end()), roi.end());    

    myprintf("ROI size: %d\n", roi.size());

    for (int i = 0; i < roi.size(); i++) {        
        roi_map[roi[i]] = true;
    }
    
    // start Proof number search
    m_root->evaluate(&m_rootstate, m_group_color, m_group_to_check);    

    int iters = 0;
    while(!m_root->solved() && ++iters < 500000) {
        m_workstate = m_rootstate;
        PNNode * most_proving = m_root->select_most_proving(&m_workstate,
                                                             m_workstate.get_to_move() == m_group_color ? 
                                                             PNNode::OR : PNNode::AND);        
        most_proving->develop_node(&m_workstate, roi_map, m_group_color, m_group_to_check);        
        most_proving->update_ancestors(m_workstate.get_to_move() == m_group_color  ? 
                                       PNNode::OR : PNNode::AND);               
        if ((iters & 1023) == 0) {
            m_workstate = m_rootstate;
            std::string pv = get_pv(&m_workstate, &(*m_root));
            myprintf("P: %d D: %d Iter: %d PV: %s\n", m_root->get_proof(), m_root->get_disproof(), iters, pv.c_str());
        }
    }

    m_workstate = m_rootstate;
    std::string pv = get_pv(&m_workstate, &(*m_root));
    myprintf("P: %d D: %d Iter: %d PV: %s\n", m_root->get_proof(), m_root->get_disproof(), iters, pv.c_str());

    return UNKNOWN;
}

std::string PNSearch::get_pv(KoState * state, PNNode * node) {    
    std::string res("");    

    PNNode * oldnode = NULL;
    PNNode * critical = node;
    PNNode::node_type_t type = state->get_to_move() == m_group_color  ? 
                               PNNode::OR : PNNode::AND;

    do {
        if (!critical->has_children()) break;

        oldnode = critical;               
        critical = critical->select_critical(type);            
        int move = critical->get_move();                
        std::string mtxt = state->board.move_to_text(move);        

        if (type == PNNode::OR) {
            type = PNNode::AND;
        } else {
            type = PNNode::OR;
        }

        res.append(mtxt);
        res.append(" ");
    } while (oldnode != critical && critical->is_expanded());

    return res;
}
