extern "C" {
    #include "../src/qth/qth.h"
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <string>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::Equals;

TEST_CASE( "Qth to lat lon", "[qth]" ) {
    double lat, lon;
    qth_str_to_pos("LO02QR82", &lat, &lon);
    REQUIRE_THAT(lat, WithinAbs(52.718750 , 1.0E-5));
    REQUIRE_THAT(lon, WithinAbs(41.404167 , 1.0E-5));
}


TEST_CASE( "Lat lon to QTH", "[qth]" ) {
    char qth[9];
    qth_pos_to_str(64.614704, 44.07084, qth);
    REQUIRE_THAT(std::string(qth), Equals("LP24ao87"));
}


TEST_CASE( "Distance between points", "[qth]" ) {
    double dist = qth_pos_dist(50.633174563518885,52.99085997976364,63.59940125996173,163.01660120487216);
    REQUIRE_THAT(dist, WithinAbs(5940.4 , 1e-1));
}
