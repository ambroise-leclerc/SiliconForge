#include "OpcodeCoverageSession.hpp"

#include <webfront/testing/Bdd.hpp>

int main(int argc, char** argv) {
    const siliconforge::testing::OpcodeCoverageSession coverage;
    return webfront::bdd::run(argc, argv);
}
