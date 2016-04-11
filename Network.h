#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

#include "config.h"
#include <vector>
#include <string>
#include <list>
#include <bitset>
#include <memory>
#ifdef USE_CAFFE
#include <caffe/caffe.hpp>
#endif

#include "FastState.h"

// Move attributes
class Network {
public:
    typedef std::bitset<19*19> BoardPlane;
    typedef std::vector<BoardPlane> NNPlanes;
    typedef std::pair<int, NNPlanes> TrainPosition;
    typedef std::vector<TrainPosition> TrainVector;

    std::vector<std::pair<float, int>> get_scored_moves(FastState * state);
    void initialize();
    void benchmark(FastState * state);
    void autotune_from_file(std::string filename);
    static Network* get_Network(void);

private:
#ifdef USE_CAFFE
    std::unique_ptr<caffe::Net<float>> net;
#endif

    void gather_traindata(std::string filename, TrainVector& tv);
    void train_network(TrainVector& tv, size_t&, size_t&);
    static void gather_features(FastState * state, NNPlanes & planes);
    static int rotate_nn_idx(int vertex, int symmetry);

    static Network* s_Net;
};

#endif
