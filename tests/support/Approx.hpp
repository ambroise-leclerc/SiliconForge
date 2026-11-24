#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace siliconforge::test {

/// A floating-point value compared within a relative epsilon (about 2.4e-5 by default) or an absolute margin:
/// `check(reading == Approx(5.0).margin(1e-6))`.
class Approx {
public:
    explicit Approx(double value) : value{value} {}
    Approx& epsilon(double relative) {
        relativeTolerance = relative;
        return *this;
    }
    Approx& margin(double absolute) {
        absoluteTolerance = absolute;
        return *this;
    }

    friend bool operator==(double other, const Approx& approx) {
        const auto close = [&](double tolerance) { return approx.value + tolerance >= other && other + tolerance >= approx.value; };
        return close(approx.absoluteTolerance) || close(approx.relativeTolerance * std::abs(approx.value));
    }
    friend bool operator==(const Approx& approx, double other) { return other == approx; }

private:
    double value;
    double relativeTolerance = static_cast<double>(std::numeric_limits<float>::epsilon()) * 100;
    double absoluteTolerance = 0;
};

}  // namespace siliconforge::test
