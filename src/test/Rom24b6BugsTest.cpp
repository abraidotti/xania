// Checks for bugs fixed in ROM 2.4b6 (https://github.com/abraidotti/2.4b6-fixed) that Xania may still have.
// Each test states the correct behaviour, so it fails (or aborts under the sanitizers) while the bug is present.
// Run them one at a time by tag: a test that aborts takes the whole binary down with it.
#include "Char.hpp"
#include "Constants.hpp"
#include "Descriptor.hpp"
#include "DescriptorList.hpp"
#include "Logging.hpp"
#include "skills.hpp"

#include <catch2/catch_test_macros.hpp>

#include "MemFile.hpp"
#include "MockMud.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

namespace {

test::MockMud mock_mud{};
DescriptorList descriptors{};
Logger logger{descriptors};

}

// 2.4b6-fixed #53 (77509e0): get_skill's range check let sn == MAX_SKILL through.
TEST_CASE("get_skill refuses an sn one past the end of the table", "[rom24b6][get_skill]") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    Char bob{mock_mud};
    bob.level = 1;
    Descriptor bob_desc{0, mock_mud};
    bob.desc = &bob_desc;
    bob_desc.character(&bob);
    bob.pcdata = std::make_unique<PcData>();

    CHECK(bob.get_skill(MAX_SKILL) == 0);
}

// 2.4b6-fixed #53 (77509e0) and Xania's one_hit (fight.cpp:469): an exotic weapon's sn is -1, and one_hit passes it
// to check_improve, which indexes the skill tables with it.
TEST_CASE("check_improve ignores sn -1, an exotic weapon's", "[rom24b6][check_improve]") {
    ALLOW_CALL(mock_mud, logger()).LR_RETURN(logger);
    Char bob{mock_mud};
    bob.level = 10;
    Descriptor bob_desc{0, mock_mud};
    bob.desc = &bob_desc;
    bob_desc.character(&bob);
    bob.pcdata = std::make_unique<PcData>();

    check_improve(&bob, -1, true, 5);
    CHECK(bob.exp == 0);
}

// Xania only (Logging.cpp:32-36): the break on '\n' leaves the outer loop, so LINE is the first line's length.
TEST_CASE("bug reports the line of the area file it was reading", "[rom24b6][bug_line]") {
    test::MemFile area("first line\nsecond line\nthird line\n");
    fseek(area.file(), 25, SEEK_SET); // inside the third line ("first line\n" is 11, "second line\n" 12)

    fflush(stderr);
    const int saved = dup(fileno(stderr));
    FILE *captured = tmpfile();
    dup2(fileno(captured), fileno(stderr));
    {
        BugAreaFileContext context("test.are", area.file());
        logger.bug("Some error message");
    }
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);

    std::string out(4096, '\0');
    rewind(captured);
    out.resize(fread(out.data(), 1, out.size(), captured));
    fclose(captured);
    INFO(out);
    CHECK(out.find("FILE: test.are LINE: 3") != std::string::npos);
}
