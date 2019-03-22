// Copyright 2017 Hakan Metin - LIP6

#include "cosy/BreakIDReader.h"

namespace cosy {

bool BreakIDReader::load(const std::string& symmetry_filename,
                       unsigned int num_vars, Group *group) {
    std::unique_ptr<Permutation> generator;
    StreamBuffer in(symmetry_filename);
    int parsed;
    Literal lit;

    while (*in && *in != 'r') {
        CHECK_EQ(*in, '(');
        generator = std::unique_ptr<Permutation>(new Permutation(num_vars));

        while (*in && *in != '\n') {
            CHECK_EQ(*in, '(');
            ++in;
            CHECK_EQ(*in, ' ');
            ++in;
            parsed = in.readInt();
            CHECK_EQ(*in, ' ');
            ++in;
            generator->addToCurrentCycle(parsed);

            while (*in != ')') {
                parsed = in.readInt();
                CHECK_EQ(*in, ' ');
                ++in;
                generator->addToCurrentCycle(parsed);
            }
            CHECK_EQ(*in, ')');
            generator->closeCurrentCycle();
            ++in;
            CHECK_EQ(*in, ' ');
            ++in;
        }
        ++in;
        group->addPermutation(std::move(generator));
    }

    return true;
}

}  // namespace cosy


/*
 * Local Variables:
 * mode: c++
 * indent-tabs-mode: nil
 * End:
 */
