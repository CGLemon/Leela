#ifndef MATCHER_H_INCLUDED
#define MATCHER_H_INCLUDED

#include <vector>
#include <boost/tr1/array.hpp>
#include <bitset>

class Matcher {
public:
    Matcher();

    float matches(int color, int pattern);

    /*
        return the "global" matcher
    */
    static Matcher* get_Matcher(void);
    static void set_Matcher(Matcher * m);

private:
    static unsigned short clip(double val);

    static Matcher* s_matcher;

    std::tr1::array<std::vector<float>, 2> m_patterns;
};

#endif
